from .runner_base import run_command, run_commands_for_routers


def container_pid(blueprint):
    pid_dict = {}
    records = run_commands_for_routers(
        blueprint["routers"],
        lambda r: [f'jq \'.pid\' /opt/lwc/containers/emu-real-{r["idx"]}/config.json'],
    )
    for rec in records:
        pid = rec.stdout.strip()
        pid_dict[rec.node] = pid

    return pid_dict, records


def getcmd_set_static_arp_pernode(router, pid_dict):
    x = router["idx"]
    pid_x = pid_dict[x]
    commands = []
    for neigh in router["neighbors"]:
        y = neigh["peeridx"]
        pid_y = pid_dict[y]
        ifname_x = neigh["ifname"]
        ifname_y = neigh["peerifname"]
        ip_y = neigh["neighbor_ip"]
        # commands.append(f"ip netns exec {pid_x} ip neigh show")
        # commands.append(f"ip netns exec {pid_y} cat /sys/class/net/{ifname_y}/address")
        commands.append(
            f"ip netns exec {pid_x} ip neigh replace {ip_y} lladdr "
            + f"$(ip netns exec {pid_y} cat /sys/class/net/{ifname_y}/address) nud permanent dev {ifname_x}"
        )
        # commands.append(f"ip netns exec {pid_x} ip neigh show")

    return commands


def getcmd_create_veth_pernode(router, pid_dict):
    idx1 = router["idx"]
    pid1 = pid_dict[idx1]

    commands = []
    for neighbor in router["neighbors"]:
        idx2 = neighbor["peeridx"]
        pid2 = pid_dict[idx2]

        if idx1 > idx2:
            continue

        veth1_name = neighbor["ifname"]
        veth2_name = neighbor["peerifname"]

        port1 = neighbor["ifname"]
        port2 = neighbor["peerifname"]

        ip1 = f"{neighbor['self_ip']}/{neighbor['selfip_prefixlen']}"
        ip2 = f"{neighbor['neighbor_ip']}/{neighbor['peerip_prefixlen']}"

        commands += [
            f"ln -sf /proc/{pid1}/ns/net /var/run/netns/{pid1}",
            f"ln -sf /proc/{pid2}/ns/net /var/run/netns/{pid2}",
            f"ip link add {veth1_name} type veth peer name {veth2_name}",
            f"ip link set {veth1_name} netns {pid1}",
            f"ip netns exec {pid1} ip link set {veth1_name} name {port1}",
            f"ip netns exec {pid1} ip link set {port1} up",
            f"ip netns exec {pid1} ip addr add {ip1} dev {port1}",
            f"ip link set {veth2_name} netns {pid2}",
            f"ip netns exec {pid2} ip link set {veth2_name} name {port2}",
            f"ip netns exec {pid2} ip link set {port2} up",
            f"ip netns exec {pid2} ip addr add {ip2} dev {port2}",
        ]

    return commands


def create_network_veth(blueprint):
    rec = run_command("mkdir -p /var/run/netns")
    pid_dict, records = container_pid(blueprint)

    records += run_commands_for_routers(
        blueprint["routers"], getcmd_create_veth_pernode, pid_dict
    )
    records += run_commands_for_routers(
        blueprint["routers"], getcmd_set_static_arp_pernode, pid_dict
    )
    return [rec] + records
