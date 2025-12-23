#!/usr/bin/env python3

import os
import sys
import json
import networkx
import random
import shutil

BGP_BASE_AS_NUMBER = 65000
WAN_PREFIX_PER_HOST = 10

"""
---------------------------------
Vendor-neutral topology definition
---------------------------------
"""


class IPPool:
    """
    Class to handle IP address generation based on prefix.

    Attributes:
        ip (list): The current IP address as a list of integers.
        needAdd (int): The number of addresses to skip based on the prefix.
        ans (list): The next available IP address in list form.

    Methods:
        alloc_ip(): Returns the next available IP address in the range.
    """

    def __init__(self, ipstr, prefix):
        self.ip = list(map(int, ipstr.split(".")))
        # Calculate the number of IP addresses to allocate based on the subnet prefix
        self.needAdd = 2 ** (32 - prefix)

    def alloc_ip(self):
        curr_ip = ".".join(str(x) for x in self.ip)
        # update and normalize self.ip
        self.ip[3] += self.needAdd
        for k in range(3, -1, -1):
            if self.ip[k] > 255:
                self.ip[k - 1] += self.ip[k] // 256
                self.ip[k] %= 256
            else:
                break
        if self.ip[0] > 255:
            raise RuntimeError("Out of IP address")
        return curr_ip


def linklocal_peer_ip(ip):
    lis = [int(x) for x in ip.split(".")]
    if lis[3] % 4 == 1:
        lis[3] += 1
    elif lis[3] % 4 == 2:
        lis[3] -= 1
    else:
        raise ValueError(f"Invalid link-local IP {ip}")
    return ".".join([str(x) for x in lis])


def create_neighbor_for_link(x, y, xip, yip, xas, yas, neighbors):
    neighbors[x].append(
        {
            "self_ip": xip,
            "selfip_prefixlen": 30,
            "neighbor_ip": yip,
            "peerip_prefixlen": 30,
            "remote_as": yas,
            "peeridx": y,
            "ifname": f"eth{x}to{y}",
            "peerifname": f"eth{y}to{x}",
        }
    )
    neighbors[y].append(
        {
            "self_ip": yip,
            "selfip_prefixlen": 30,
            "neighbor_ip": xip,
            "peerip_prefixlen": 30,
            "remote_as": xas,
            "peeridx": x,
            "ifname": f"eth{y}to{x}",
            "peerifname": f"eth{x}to{y}",
        }
    )


def generate_blueprint_fattree(k):
    def _asn(k):
        """
        Create a BGP AS number mapping for a k-pod FatTree topology.

        Args:
            k: The number of pods in the FatTree topology.

        Returns:
            A dictionary where keys are node IDs, and values are their corresponding BGP AS numbers.
        """
        node_asn = {}
        for pod in range(k):
            for tor in range(k // 2):
                torx = get_tor_id(pod, tor, k)
                tor_asn = pod * k // 2 + tor
                node_asn[torx] = BGP_BASE_AS_NUMBER + tor_asn
            leaf_asn = k * k // 2 + pod
            for leaf in range(k // 2):
                leafx = get_leaf_id(pod, leaf, k)
                node_asn[leafx] = BGP_BASE_AS_NUMBER + leaf_asn
        spine_asn = k * k // 2 + k
        for leaf in range(k // 2):
            for spine in range(k * k // 2):
                spinex = get_spine_id(leaf, spine, k)
                node_asn[spinex] = BGP_BASE_AS_NUMBER + spine_asn
        return node_asn

    def get_tor_id(pod, tor, k):
        return pod * k + tor + 1

    def get_leaf_id(pod, leaf, k):
        return pod * k + k // 2 + leaf + 1

    def get_spine_id(leaf, spine, k):
        return k * k + leaf * k // 2 + spine + 1

    n_nodes = k * k // 4 * 5

    linkip_pool = IPPool("169.0.0.1", 30)
    routerid_pool = IPPool("10.1.1.0", 32)
    node_rid = {i: routerid_pool.alloc_ip() for i in range(1, n_nodes + 1)}
    node_asn = _asn(k)

    networkip_pool = IPPool("11.1.1.0", 24)
    node_network = {x: [] for x in range(1, n_nodes + 1)}
    for pod in range(k):
        for tor in range(k // 2):
            tor_id = get_tor_id(pod, tor, k)
            for _ in range(k // 2):
                node_network[tor_id].append(networkip_pool.alloc_ip())

    # Create blueprint structure
    blueprint = {"routers": []}
    node_neighbors = {i: [] for i in range(1, n_nodes + 1)}

    for pod in range(k):
        # ToR ↔ Leaf links (within Pod)
        for tor in range(k // 2):
            tor_id = get_tor_id(pod, tor, k)
            for leaf in range(k // 2):
                leaf_id = get_leaf_id(pod, leaf, k)
                tor_ip = linkip_pool.alloc_ip()
                leaf_ip = linklocal_peer_ip(tor_ip)
                create_neighbor_for_link(
                    tor_id,
                    leaf_id,
                    tor_ip,
                    leaf_ip,
                    node_asn[tor_id],
                    node_asn[leaf_id],
                    node_neighbors,
                )

        # Leaf ↔ Spine links (within Pod)
        for leaf in range(k // 2):
            leaf_id = get_leaf_id(pod, leaf, k)
            for spine in range(k // 2):
                spine_id = get_spine_id(leaf, spine, k)
                leaf_ip = linkip_pool.alloc_ip()
                spine_ip = linklocal_peer_ip(leaf_ip)
                create_neighbor_for_link(
                    leaf_id,
                    spine_id,
                    leaf_ip,
                    spine_ip,
                    node_asn[leaf_id],
                    node_asn[spine_id],
                    node_neighbors,
                )

    blueprint["routers"] = [
        {
            "idx": x,
            "bgpid": node_rid[x],
            "bgp_as": node_asn[x],
            "neighbors": node_neighbors[x],
            "networks": node_network[x],
        }
        for x in range(1, n_nodes + 1)
    ]

    return blueprint


def check_topo_connected(nodes, edges):
    def dfs(u, visited: set, edges):
        for v in edges[u]:
            if v in visited:
                continue
            visited.add(v)
            dfs(v, visited, edges)

    visited = set()
    dfs(nodes[0], visited, edges)
    assert len(visited) == len(nodes)


def read_gml(gml_path):
    # Original gml have duplicated edges, mark it as multigraph to prevent networkx from panic
    with open(gml_path, "r") as f:
        lines = f.readlines()
    lines.insert(1, "  multigraph 1\n")

    # label="id" to eliminate duplicated nodes
    graph = networkx.Graph(networkx.parse_gml(lines, label="id"))

    # `check_topo_connected` will check for this case, together with islands
    # with more than 1 node. Let problematic topology panic earlier, so don't
    # remove those isolated nodes.
    """
    isolated_node = []
    for node in graph.nodes:
        if len(list(graph.neighbors(node))) == 0:
            isolated_node.append(node)
    for node in isolated_node:
        graph.remove_node(node)
    """

    return networkx.relabel_nodes(
        graph, {x: idx + 1 for idx, x in enumerate(graph.nodes)}
    )


def generate_blueprint_from_graph(G):
    nodes = list(G.nodes)
    edges = {u: list(G.neighbors(u)) for u in nodes}
    check_topo_connected(nodes, edges)

    linkip_pool = IPPool("169.254.1.1", 30)
    routerid_pool = IPPool("10.1.1.0", 32)
    node_rid = {i: routerid_pool.alloc_ip() for i in nodes}
    node_asn = {i: BGP_BASE_AS_NUMBER + i for i in nodes}

    networkip_pool = IPPool("11.1.1.0", 24)
    node_network = {x: [] for x in nodes}
    for x in nodes:
        for _ in range(WAN_PREFIX_PER_HOST):
            node_network[x].append(networkip_pool.alloc_ip())

    # Create blueprint structure
    blueprint = {"routers": []}
    neighbors = {i: [] for i in nodes}

    for x, y_list in edges.items():
        for y in y_list:
            # without this if statement, each edge will be added twice
            if x > y:
                continue
            xip = linkip_pool.alloc_ip()
            yip = linklocal_peer_ip(xip)
            create_neighbor_for_link(
                x, y, xip, yip, node_asn[x], node_asn[y], neighbors
            )

    blueprint["routers"] = [
        {
            "idx": x,
            "bgpid": node_rid[x],
            "bgp_as": node_asn.get(x, BGP_BASE_AS_NUMBER),
            "neighbors": neighbors[x],
            "networks": node_network[x],
        }
        for x in nodes
    ]

    return blueprint


def generate_blueprint_topozoo(toponame):
    gml_path = f"config/topozoo/{toponame}.gml"
    if not os.path.exists(gml_path):
        raise ValueError(f"Topology {toponame} not found.")

    G = read_gml(gml_path)
    return generate_blueprint_from_graph(G)


def generate_blueprint_dupzoo(topoid):
    toponame, _ncopy = topoid.split(":")
    ncopy = int(_ncopy)
    gml_path = f"config/topozoo/{toponame}.gml"
    if not os.path.exists(gml_path):
        raise ValueError(f"Topology {toponame} not found.")

    seed = read_gml(gml_path)
    seed_siz = len(seed.nodes)
    G = networkx.Graph()
    for i in range(ncopy):
        G.add_nodes_from([x + seed_siz * i for x in seed.nodes])
        G.add_edges_from(
            [
                (u + seed_siz * i, v + seed_siz * i)
                for u in seed.nodes
                for v in seed.neighbors(u)
            ]
        )
        if i == 0:
            continue

        # add random edge to keep graph connected
        u = random.randint(seed_siz * i + 1, seed_siz * (i + 1))
        v = random.randint(1, seed_siz * i)
        while v == u:
            v = random.randint(1, seed_siz * i)
        print(f"add_edge {u} -- {v}")
        G.add_edge(u, v)

    return generate_blueprint_from_graph(G)


def generate_blueprint(topo_type, topo_id):
    if topo_type == "fattree":
        return generate_blueprint_fattree(int(topo_id))
    elif topo_type == "topozoo":
        return generate_blueprint_topozoo(topo_id)
    elif topo_type == "dupzoo":
        return generate_blueprint_dupzoo(topo_id)
    else:
        raise NotImplementedError(f"Unsupported topo_type: {topo_type}")


"""
---------------------------------
Vendor-specific config generation
---------------------------------
"""


def config_gen_frr(blueprint, path):
    config_template = """log file /var/log/real/frr.log debugging

router bgp {bgp_as}
bgp router-id {router_id}
no bgp ebgp-requires-policy
no bgp network import-check

{network_config}

{neighbor_config}
bgp bestpath as-path multipath-relax
address-family ipv4 unicast
{neighbor_activate}
maximum-paths 64
exit-address-family
"""
    for router in blueprint["routers"]:
        network_config = ""
        neighbor_config = ""
        neighbor_activate = ""

        for network in router["networks"]:
            network_config += f"network {network}/24\n"

        for neighbor in router["neighbors"]:
            neighbor_config += f"neighbor {neighbor['neighbor_ip']} remote-as {neighbor['remote_as']}\n"
            neighbor_config += f"neighbor {neighbor['neighbor_ip']} timers 0 0\n"
            neighbor_config += (
                f"neighbor {neighbor['neighbor_ip']} timers connect 240\n"
            )
            neighbor_activate += f"neighbor {neighbor['neighbor_ip']} activate\n"

        config_text = config_template.format(
            bgp_as=router["bgp_as"],
            router_id=router["bgpid"],
            network_config=network_config,
            neighbor_config=neighbor_config,
            neighbor_activate=neighbor_activate,
        )

        with open(f"{path}/node_{router['idx']}.conf", "w") as f:
            f.write(config_text)


def config_gen_bird(blueprint, path):
    config_template = """log "/var/log/real/bird.log" all;
router id {router_id};
protocol device {{ scan time 65535; }}

# protocol kernel {{
#     ipv4 {{
#         export all;
#         import all;
#     }};
#     persist;
# }}

# network config
{network_config}
# neighbor config
{neighbor_config}
"""
    network_template = """protocol static static{i} {{
    ipv4;
    route {network}/24 blackhole;
}}
"""
    neighbor_template = """protocol bgp bgp{i} {{
    local as {self_asn};
    neighbor {neigh_ip} as {neigh_asn};
    ipv4 {{
        import all;
        export all;
    }};
    hold time 0;
    startup hold time 65535;
    connect delay time 2;
}}
"""

    for router in blueprint["routers"]:
        network_config = ""
        neighbor_config = ""

        # network
        for i, network in enumerate(router["networks"]):
            network_config += network_template.format(i=i + 1, network=network)

        # neighbor
        for i, neighbor in enumerate(router["neighbors"]):
            group_text = neighbor_template.format(
                i=i + 1,
                self_asn=router["bgp_as"],
                neigh_asn=neighbor["remote_as"],
                neigh_ip=neighbor["neighbor_ip"],
            )
            neighbor_config += group_text

        config_text = config_template.format(
            router_id=router["bgpid"],
            network_config=network_config,
            neighbor_config=neighbor_config,
        )

        with open(f"{path}/node_{router['idx']}.conf", "w") as f:
            f.write(config_text)


def config_gen_crpd(blueprint, path):
    config_template = """system {{
    host-name {crpd_name};
}}
{interfaces_config}
policy-options {{
    policy-statement send-static {{
        term block {{
            from {{
                route-filter 169.0.0.0/8 longer;
            }}
            then reject;
        }}
        term others {{
            then accept;
        }}
    }}
}}
routing-options {{
    autonomous-system {bgp_as};
    maximum-ecmp 64;
    router-id {router_id};
    {network_config}
}}
protocols {{
    bgp {{
        multipath;
        hold-time 0;
        {neighbor_config}
    }}
}}
policy-options {{
    policy-statement send-static {{
        term 1 {{
            then {{
                accept;
            }}
        }}
    }}
}}
"""
    interface_template = """    eth{i} {{
        unit 0 {{
            family inet {{
                address {ip}/{prefixlen};
            }}
        }}
    }}
"""
    # cannot configure keepalive seperately in crpd, hold-time=3*keepalive
    neighbor_template = """group to_{remote_as} {{
            type external;
            import send-static;
            export send-static;
            neighbor {neighbor_ip} {{
                peer-as {remote_as};
            }}
        }}
"""

    for router in blueprint["routers"]:
        network_config = ""
        neighbor_config = ""

        # interface
        if len(router["neighbors"]) > 0:
            interfaces_config = "interfaces {\n"
            for i in range(len(router["neighbors"])):
                neighbor = router["neighbors"][i]
                interface_text = interface_template.format(
                    i=i, ip=neighbor["self_ip"], prefixlen=neighbor["selfip_prefixlen"]
                )
                interfaces_config += interface_text
            interfaces_config += "}"
        else:
            interfaces_config = ""

        # network
        if len(router["networks"]) > 0:
            network_config = "    static {\n"
            for network in router["networks"]:
                network_config += f"        route {network}/24 discard;\n"
            network_config += "    }\n"

        # neighbor
        for neighbor in router["neighbors"]:
            group_text = neighbor_template.format(
                remote_as=neighbor["remote_as"], neighbor_ip=neighbor["neighbor_ip"]
            )
            neighbor_config += group_text

        # neighbor_config += '''traceoptions {
        #     file bgp.log size 10m;
        #     flag all detail;
        # }'''

        config_text = config_template.format(
            bgp_as=router["bgp_as"],
            router_id=router["bgpid"],
            crpd_name="crpd" + str(router["idx"]),
            network_config=network_config,
            neighbor_config=neighbor_config,
            interfaces_config=interfaces_config,
        )

        with open(f"{path}/node_{router['idx']}.conf", "w") as f:
            f.write(config_text)


def config_gen(image, blueprint, path):
    if image == "frr":
        config_gen_frr(blueprint, path)
    elif image == "crpd":
        config_gen_crpd(blueprint, path)
    elif image == "bird":
        config_gen_bird(blueprint, path)
    else:
        raise NotImplementedError(f"Unsupported image: {image}")


def main():
    random.seed(0)
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <image> <topo_type> <topo_id>")
        print(f"Example:")
        print(f"  {sys.argv[0]} frr fattree 2")
        print(f"  {sys.argv[0]} bird topozoo Kdl")
        print(f"  {sys.argv[0]} crpd dupzoo Fccn:2")
        return

    image = sys.argv[1]
    topo_type = sys.argv[2]
    topo_id = sys.argv[3]

    if image not in ["frr", "crpd", "bird"]:
        raise NotImplementedError(f"Unsupported Image: {image}")
    if topo_type not in ["fattree", "topozoo", "dupzoo"]:
        raise NotImplementedError(f"Unsupported topo_type: {topo_type}")

    blueprint = generate_blueprint(topo_type, topo_id)

    if topo_type == "fattree":
        path = f"conf/{image}/fattree{topo_id}"
    elif topo_type == "topozoo":
        path = f"conf/{image}/topozoo_{topo_id}"
    elif topo_type == "dupzoo":
        topo_name, topo_copy = topo_id.split(":")
        path = f"conf/{image}/topozoo_{topo_name}_dup{topo_copy}"
    else:
        raise NotImplementedError(f"Unsupported topology type: {topo_type}")

    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path)
    with open(f"{path}/blueprint.json", "w") as f:
        json.dump(blueprint, f, indent=4)

    config_gen(image, blueprint, path)
    print(f"{path}")


if __name__ == "__main__":
    main()
