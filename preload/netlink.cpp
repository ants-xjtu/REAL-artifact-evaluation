#include "netlink.h"
#include "preload.h"
#include "util.h"
#include "debug.h"
#include <cstring>
#include <cassert>

thread_local static std::map<std::string, int> if_name_to_idx;
thread_local static std::map<int, struct netif> if_list;

#define FILL_RTA_INT8(type, value) \
    {\
        /* assert(RTA_OK(rta, len)); */\
        rta->rta_type = type;\
        *(int8_t *)RTA_DATA(rta) = value;\
        rta->rta_len = RTA_LENGTH(sizeof(int8_t));\
        rta = RTA_NEXT(rta, len);\
    }

#define FILL_RTA_INT32(type, value) \
    {\
        /* assert(RTA_OK(rta, len)); */ \
        rta->rta_type = type;\
        *(int32_t *)RTA_DATA(rta) = value;\
        rta->rta_len = RTA_LENGTH(sizeof(int32_t));\
        rta = RTA_NEXT(rta, len);\
    }

#define FILL_RTA_STR(type, str) \
    {\
        /* assert(RTA_OK(rta, len)); */ \
        rta->rta_type = type;\
        rta->rta_len = RTA_LENGTH(str.length() + 1);\
        memcpy((char *)RTA_DATA(rta), str.c_str(), str.length() + 1);\
        rta = RTA_NEXT(rta, len);\
    }

#define FILL_RTA_BYTES(type, length, value) \
    {\
        /* assert(RTA_OK(rta, len)); */ \
        rta->rta_type = type;\
        rta->rta_len = RTA_LENGTH(length);\
        memcpy((char *)RTA_DATA(rta), value, length);\
        rta = RTA_NEXT(rta, len);\
    }

static void
add_loopback(
    int if_idx,
    std::string ifname,
    struct in_addr sip,
    struct in_addr pip,
    hwaddr self_mac,
    hwaddr broadcast_mac
)
{
    if_name_to_idx[ifname] = if_idx;
    struct netif netif = {
        .idx = if_idx,
        .name = ifname,
        .if_type = IFTYPE_LOOPBACK,
        .ipv4_self_addr = {sip},
        .ipv4_peer_addr = {pip},
        .ifinfo = {
            .ifi_family = AF_UNSPEC,
            .ifi_type = ARPHRD_LOOPBACK,
            .ifi_index = if_idx,
            .ifi_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING | IFF_LOWER_UP,
            .ifi_change = 0,
        },
        .ifla = {
            .txqlen = 1000,
            .operstate = 0,
            .linkmode = 0,
            .mtu = 65536,
            .min_mtu = 0,
            .max_mtu = 0,
            .group = 0,
            .promiscuity = 0,
            .num_tx_queues = 1,
            .gso_max_segs = 65535,
            .gso_max_size = 65536,
            .num_rx_queues = 1,
            .carrier = 1,
            .qdisc = "noqueue",
            .carrier_changes = 0,
            .carrier_up_count = 0,
            .carrier_down_count = 0,
            .proto_down = 0,
            .ifmap = {
                .mem_start = 0,
                .mem_end = 0,
                .base_addr = 0,
                .irq = 0,
                .dma = 0,
                .port = 0
            },
            .hw_addr = self_mac,
            .hw_broadcast = broadcast_mac,
            .stats64 = {0},
            .stats = {0},
            .xdp = {.xdp_attached = XDP_ATTACHED_NONE},
            .af_spec = {0}
        }
    };
    if_list[if_idx] = netif;
}

static void
add_veth(
    int if_idx,
    std::string ifname,
    struct in_addr sip,
    struct in_addr pip,
    hwaddr self_mac,
    hwaddr broadcast_mac
)
{
    if_name_to_idx[ifname] = if_idx;
    struct netif netif = {
        .idx = if_idx,
        .name = ifname,
        .if_type = IFTYPE_VETH,
        .ipv4_self_addr = {sip},
        .ipv4_peer_addr = {pip},
        .ifinfo = {
            .ifi_family = AF_UNSPEC,
            .ifi_type = ARPHRD_ETHER,
            .ifi_index = if_idx,
            .ifi_flags = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST | IFF_LOWER_UP,
            .ifi_change = 0,
        },
        .ifla = {
            .txqlen = 1000,
            .operstate = 6,
            .linkmode = 0,
            .mtu = 1500,
            .min_mtu = 68,
            .max_mtu = 65535,
            .group = 0,
            .promiscuity = 0,
            .num_tx_queues = 24,
            .gso_max_segs = 65535,
            .gso_max_size = 65536,
            .num_rx_queues = 24,
            .carrier = 1,
            .qdisc = "noqueue",
            .carrier_changes = 2,
            .carrier_up_count = 1,
            .carrier_down_count = 1,
            .proto_down = 0,
            .ifmap = {
                .mem_start = 0,
                .mem_end = 0,
                .base_addr = 0,
                .irq = 0,
                .dma = 0,
                .port = 0
            },
            .hw_addr = self_mac,
            .hw_broadcast = broadcast_mac,
            .stats64 = {0},
            .stats = {0},
            .xdp = {.xdp_attached = XDP_ATTACHED_NONE},
            .af_spec = {0}
        }
    };
    if_list[if_idx] = netif;
}

void
add_if(
    int idx,
    std::string ifname,
    IFTYPE type,
    struct in_addr sip,
    struct in_addr pip,
    hwaddr self_mac,
    hwaddr broadcast_mac
)
{
    switch (type) {
    case IFTYPE_LOOPBACK:
        add_loopback(idx, ifname, sip, pip, self_mac, broadcast_mac);
        break;
    case IFTYPE_VETH:
        add_veth(idx, ifname, sip, pip, self_mac, broadcast_mac);
        break;
    default:
        assert(0);
    }
}

static netlink_response
make_nlmsg_done(const struct nlmsghdr *h)
{
    struct netlink_response resp;
    struct nlmsghdr *resp_h = &resp.n;
    resp_h->nlmsg_type = NLMSG_DONE;
    resp_h->nlmsg_flags = NLM_F_MULTI;
    resp_h->nlmsg_seq = h->nlmsg_seq;
    resp_h->nlmsg_pid = h->nlmsg_pid;
    resp_h->nlmsg_len = 20;
    *(int *)(resp_h + 1) = 0;
    return resp;
}

static netlink_response
make_nlmsg_error(const struct nlmsghdr *h)
{
    struct netlink_response resp;
    struct nlmsghdr *resp_h = &resp.n;
    resp_h->nlmsg_type = NLMSG_ERROR;
    resp_h->nlmsg_flags = NLM_F_CAPPED;
    resp_h->nlmsg_seq = h->nlmsg_seq;
    resp_h->nlmsg_pid = h->nlmsg_pid;

    struct nlmsgerr *err_msg = (struct nlmsgerr *)(resp_h + 1);
    err_msg->error = 0;
    err_msg->msg = *h;

    resp_h->nlmsg_len = sizeof(*resp_h) + sizeof(*err_msg);
    return resp;
}

static netlink_response
getlink_loopback(
    __u32 seq, __u32 pid,
    struct netif &interface
)
{
    int if_idx = interface.idx;

    struct netlink_response resp_real;
    struct getlink_response *resp = (struct getlink_response *)&resp_real;
    struct nlmsghdr *resp_h = &resp->n;
    // set len later
    resp_h->nlmsg_type = RTM_NEWLINK;
    resp_h->nlmsg_flags = NLM_F_MULTI;
    resp_h->nlmsg_seq = seq;
    resp_h->nlmsg_pid = pid;

    resp->ifinfomsg = interface.ifinfo;

    // fill attrs
    int len = sizeof(resp->buf);
    struct rtattr *rta = resp->attrs;

    struct netif::ifla &ifla = interface.ifla;

    FILL_RTA_STR(IFLA_IFNAME, interface.name);
    FILL_RTA_INT32(IFLA_TXQLEN, ifla.txqlen);
    FILL_RTA_INT8(IFLA_OPERSTATE, ifla.operstate); // changes
    FILL_RTA_INT8(IFLA_LINKMODE, ifla.linkmode);
    FILL_RTA_INT32(IFLA_MTU, ifla.mtu); // lo: 65536, eth: 1500
    FILL_RTA_INT32(IFLA_MIN_MTU, ifla.min_mtu); // lo: 0, eth: 68
    FILL_RTA_INT32(IFLA_MAX_MTU, ifla.max_mtu); // lo: 0, eth: 65535
    FILL_RTA_INT32(IFLA_GROUP, ifla.group);
    FILL_RTA_INT32(IFLA_PROMISCUITY, ifla.promiscuity);
    FILL_RTA_INT32(IFLA_NUM_TX_QUEUES, ifla.num_tx_queues); // lo: 1, eth: 24
    FILL_RTA_INT32(IFLA_GSO_MAX_SEGS, ifla.gso_max_segs);
    FILL_RTA_INT32(IFLA_GSO_MAX_SIZE, ifla.gso_max_size);
    FILL_RTA_INT32(IFLA_NUM_RX_QUEUES, ifla.num_rx_queues); // lo: 1, eth: 24
    FILL_RTA_INT8(IFLA_CARRIER, ifla.carrier);
    FILL_RTA_STR(IFLA_QDISC, ifla.qdisc);
    FILL_RTA_INT32(IFLA_CARRIER_CHANGES, ifla.carrier_changes);
    FILL_RTA_INT32(IFLA_CARRIER_UP_COUNT, ifla.carrier_up_count);
    FILL_RTA_INT32(IFLA_CARRIER_DOWN_COUNT, ifla.carrier_down_count);
    FILL_RTA_INT8(IFLA_PROTO_DOWN, 0);
    // TODO: IFLA_MAP
    FILL_RTA_BYTES(IFLA_ADDRESS, ifla.hw_addr.hw_addr_len, (const char *)ifla.hw_addr.hw_addr);
    FILL_RTA_BYTES(IFLA_BROADCAST, ifla.hw_broadcast.hw_addr_len, (const char *)ifla.hw_broadcast.hw_addr);
    // TODO: IFLA_STATS64
    // TODO: IFLA_STATS
    // TODO: IFLA_XDP

    // things below are eth only, and frr cares
    // IFLA_AF_SPEC
    // assert(RTA_OK(rta, len));
    // seems that frr only care about VLAN-related things in AF_SPEC,
    // since we don't use VLAN, we fill IFLA_AF_SPEC with dumb value
    rta->rta_type = IFLA_AF_SPEC;
    rta->rta_len = 8; // TODO: don't hard code this
    RTA_NEXT(rta, len);
        // nested
        // assert(RTA_OK(rta, len));
        rta->rta_type = IFLA_UNSPEC;
        rta->rta_len = 4; // TODO: don't hard code this
        RTA_NEXT(rta, len);

    resp_h->nlmsg_len = ((char *)rta - (char *)resp);
    return resp_real;
}

static netlink_response
getlink_veth(
    __u32 seq, __u32 pid,
    struct netif &interface
)
{
    int if_idx = interface.idx;

    struct netlink_response resp_real;
    struct getlink_response *resp = (struct getlink_response *)&resp_real;
    struct nlmsghdr *resp_h = &resp->n;
    // set len later
    resp_h->nlmsg_type = RTM_NEWLINK;
    resp_h->nlmsg_flags = NLM_F_MULTI;
    resp_h->nlmsg_seq = seq;
    resp_h->nlmsg_pid = pid;

    resp->ifinfomsg = interface.ifinfo;

    // fill attrs
    int len = sizeof(resp->buf);
    struct rtattr *rta = resp->attrs;

    struct netif::ifla &ifla = interface.ifla;

    FILL_RTA_STR(IFLA_IFNAME, interface.name);
    FILL_RTA_INT32(IFLA_TXQLEN, ifla.txqlen);
    FILL_RTA_INT8(IFLA_OPERSTATE, ifla.operstate); // changes
    FILL_RTA_INT8(IFLA_LINKMODE, ifla.linkmode);
    FILL_RTA_INT32(IFLA_MTU, ifla.mtu); // lo: 65536, eth: 1500
    FILL_RTA_INT32(IFLA_MIN_MTU, ifla.min_mtu); // lo: 0, eth: 68
    FILL_RTA_INT32(IFLA_MAX_MTU, ifla.max_mtu); // lo: 0, eth: 65535
    FILL_RTA_INT32(IFLA_GROUP, ifla.group);
    FILL_RTA_INT32(IFLA_PROMISCUITY, ifla.promiscuity);
    FILL_RTA_INT32(IFLA_NUM_TX_QUEUES, ifla.num_tx_queues); // lo: 1, eth: 24
    FILL_RTA_INT32(IFLA_GSO_MAX_SEGS, ifla.gso_max_segs);
    FILL_RTA_INT32(IFLA_GSO_MAX_SIZE, ifla.gso_max_size);
    FILL_RTA_INT32(IFLA_NUM_RX_QUEUES, ifla.num_rx_queues); // lo: 1, eth: 24
    FILL_RTA_INT8(IFLA_CARRIER, ifla.carrier);
    FILL_RTA_STR(IFLA_QDISC, ifla.qdisc);
    FILL_RTA_INT32(IFLA_CARRIER_CHANGES, ifla.carrier_changes);
    FILL_RTA_INT32(IFLA_CARRIER_UP_COUNT, ifla.carrier_up_count);
    FILL_RTA_INT32(IFLA_CARRIER_DOWN_COUNT, ifla.carrier_down_count);
    FILL_RTA_INT8(IFLA_PROTO_DOWN, 0);
    // TODO: IFLA_MAP
    FILL_RTA_BYTES(IFLA_ADDRESS, ifla.hw_addr.hw_addr_len, (const char *)ifla.hw_addr.hw_addr);
    FILL_RTA_BYTES(IFLA_BROADCAST, ifla.hw_broadcast.hw_addr_len, (const char *)ifla.hw_broadcast.hw_addr);
    // TODO: IFLA_STATS64
    // TODO: IFLA_STATS
    // TODO: IFLA_XDP

    // things below are eth only, and frr cares
    // IFLA_LINKINFO
    // assert(RTA_OK(rta, len));
    rta->rta_type = IFLA_LINKINFO;
    rta->rta_len = sizeof(*rta) + sizeof(*rta) + ALIGN_UP(ifla.linkinfo.info_kind.length() + 1, 4); // TODO: check whether this is 16
    RTA_NEXT(rta, len);
        // nested
        FILL_RTA_STR(IFLA_INFO_KIND, ifla.linkinfo.info_kind);
    FILL_RTA_INT32(IFLA_LINK_NETNSID, 0);
    FILL_RTA_INT32(IFLA_LINK, if_idx);
    // IFLA_AF_SPEC
    // assert(RTA_OK(rta, len));
    // seems that frr only care about VLAN-related things in AF_SPEC,
    // since we don't use VLAN, we fill IFLA_AF_SPEC with dumb value
    rta->rta_type = IFLA_AF_SPEC;
    rta->rta_len = 8; // TODO: don't hard code this
    RTA_NEXT(rta, len);
        // nested
        // assert(RTA_OK(rta, len));
        rta->rta_type = IFLA_UNSPEC;
        rta->rta_len = 4; // TODO: don't hard code this
        RTA_NEXT(rta, len);

    resp_h->nlmsg_len = ((char *)rta - (char *)resp);
    return resp_real;
}

int
netlink_fdesc::inject_links(
    const struct nlmsghdr *h,
    int req_seq
)
{
    LOG("inject_links @ %d\n", req_seq);
    __u32 seq = h->nlmsg_seq;
    __u32 pid = h->nlmsg_pid;
    for (auto &p : if_list) {
        struct netif &interface = p.second;
        LOG("interface %d, %s\n", p.first, p.second.name.c_str());
        switch (interface.if_type) {
        case IFTYPE_LOOPBACK: {
            auto resp = getlink_loopback(seq, pid, interface);
            resp_que[req_seq].emplace(resp);
        }
            break;
        case IFTYPE_VETH: {
            auto resp = getlink_veth(seq, pid, interface);
            resp_que[req_seq].emplace(resp);
        }
            break;
        default:
            LOG("wrong IFTYPE %d\n", interface.if_type);
            assert(0);
            break;
        }
    }
    return 0;
}

int
netlink_fdesc::handle_getnetconf_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    for (auto af : std::vector<__u8>{AF_INET, AF_INET6}) {
        for (auto &p : if_list) {
            auto &if_idx = p.first;

            struct netlink_response resp_real;
            struct getnetconf_response *resp = (struct getnetconf_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWNETCONF;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct netconfmsg`
            resp->netconfmsg = { .ncm_family = af };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_INT32(NETCONFA_IFINDEX, if_idx);
            FILL_RTA_INT32(NETCONFA_FORWARDING, (int)(af == AF_INET));
            if (af == AF_INET) {
                FILL_RTA_INT32(NETCONFA_RP_FILTER, 2);
            }
            FILL_RTA_INT32(NETCONFA_MC_FORWARDING, 0);
            if (af == AF_INET) {
                FILL_RTA_INT32(NETCONFA_BC_FORWARDING, 0);
            }
            FILL_RTA_INT32(NETCONFA_PROXY_NEIGH, 0);
            FILL_RTA_INT32(NETCONFA_IGNORE_ROUTES_WITH_LINKDOWN, 0);

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }
        std::vector<int> if_idx_list = {NETCONFA_IFINDEX_ALL, NETCONFA_IFINDEX_DEFAULT};
        for (auto if_idx : if_idx_list)
        {
            struct netlink_response resp_real;
            struct getnetconf_response *resp = (struct getnetconf_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWNETCONF;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct netconfmsg`
            resp->netconfmsg = { .ncm_family = af };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_INT32(NETCONFA_IFINDEX, if_idx);
            FILL_RTA_INT32(NETCONFA_FORWARDING, (int)(af == AF_INET));
            if (af == AF_INET) {
                FILL_RTA_INT32(NETCONFA_RP_FILTER, 2);
            }
            FILL_RTA_INT32(NETCONFA_MC_FORWARDING, 0);
            if (af == AF_INET) {
                FILL_RTA_INT32(NETCONFA_BC_FORWARDING, 0);
            }
            FILL_RTA_INT32(NETCONFA_PROXY_NEIGH, 0);
            FILL_RTA_INT32(NETCONFA_IGNORE_ROUTES_WITH_LINKDOWN, 0);

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }
    }
    resp_que[req_seq].emplace(make_nlmsg_done(h));
    return h->nlmsg_len;
}

int
netlink_fdesc::handle_getnexthop_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    resp_que[req_seq].emplace(make_nlmsg_done(h));
    return h->nlmsg_len;
}

int
netlink_fdesc::handle_getvlan_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    resp_que[req_seq].emplace(make_nlmsg_done(h));
    return h->nlmsg_len;
}

void
netlink_fdesc::getrule_inet(
    const struct nlmsghdr *h,
    int req_seq
)
{
    // table local
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_LOCAL,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_LOCAL);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
    // table main
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_LOCAL,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_MAIN);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);
        FILL_RTA_INT32(FRA_PRIORITY, 32766);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
    // table default
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_DEFAULT,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_DEFAULT);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);
        FILL_RTA_INT32(FRA_PRIORITY, 32767);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
}

void
netlink_fdesc::getrule_inet6(
    const struct nlmsghdr *h,
    int req_seq
)
{
        // table local
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET6,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_LOCAL,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_LOCAL);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
    // table main
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET6,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_LOCAL,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_MAIN);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);
        FILL_RTA_INT32(FRA_PRIORITY, 32766);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
    // table default
    {
        struct netlink_response resp_real;
        struct getrule_response *resp = (struct getrule_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWRULE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct fib_rule_hdr`
        resp->rulemsg = {
            .family = AF_INET6,
            .dst_len = 0,
            .src_len = 0,
            .tos = 0,
            .table = RT_TABLE_DEFAULT,
            .action = FR_ACT_TO_TBL,
            .flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(FRA_TABLE, RT_TABLE_DEFAULT);
        FILL_RTA_INT32(FRA_SUPPRESS_PREFIXLEN, -1);
        FILL_RTA_INT8(FRA_PROTOCOL, RTPROT_KERNEL);
        FILL_RTA_INT32(FRA_PRIORITY, 32767);

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
}

int
netlink_fdesc::handle_getrule_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    struct fib_rule_hdr *msghdr = (struct fib_rule_hdr *)NLMSG_DATA(h);
    switch (msghdr->family) {
    case AF_INET:
        getrule_inet(h, req_seq);
        break;
    case AF_INET6:
        getrule_inet6(h, req_seq);
        break;
    default:
        break;
    }
    resp_que[req_seq].emplace(make_nlmsg_done(h));
    return h->nlmsg_len;
}

int
netlink_fdesc::inject_qdiscs(
    const struct nlmsghdr *h,
    int req_seq
)
{
    LOG("inject_qdiscs @ %d\n", req_seq);
    struct tcmsg *msghdr = (struct tcmsg *)NLMSG_DATA(h);
    for (auto &p : if_list) {
        auto if_idx = p.first;
        auto &interface = p.second;
        {
            struct netlink_response resp_real;
            struct getqdisc_response *resp = (struct getqdisc_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWRULE;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct tcmsg`
            resp->tcmsg = {
                .tcm_family = AF_UNSPEC,
                .tcm_ifindex = if_idx,
                .tcm_handle = msghdr->tcm_handle,
                .tcm_parent = __u32(-1),
                .tcm_info = 2
            };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_STR(TCA_KIND, interface.ifla.qdisc); // todo: is there really a match?
            FILL_RTA_INT8(TCA_HW_OFFLOAD, 0);
            // TODO: TCA_STATS2
            // TODO: TCA_STATS

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }
    }
    return 0;
}

#define RTM_NHA(h) \
    ((struct rtattr *)(((char *)(h)) + NLMSG_ALIGN(sizeof(struct nhmsg))))

int
netlink_fdesc::handle_newnexthop_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    resp_que[req_seq].emplace(make_nlmsg_error(h));
    return h->nlmsg_len;
}


int
netlink_fdesc::handle_delnexthop_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    resp_que[req_seq].emplace(make_nlmsg_error(h));
    return h->nlmsg_len;
}


int
netlink_fdesc::handle_newroute_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    resp_que[req_seq].emplace(make_nlmsg_error(h));
    return h->nlmsg_len;
}


int
netlink_fdesc::inject_addrs(
    const struct nlmsghdr *h,
    int req_seq
)
{
    LOG("inject_addrs @ %d\n", req_seq);
    // TODO: check ifa_family, handle inet6 properly
    struct ifaddrmsg *req_msg = (struct ifaddrmsg *)NLMSG_DATA(h);
    if (req_msg->ifa_family != AF_INET) {
        LOG("[INFO] req_msg->ifa_family = %d\n", req_msg->ifa_family);
    }
    for (auto &p : if_name_to_idx) {
        auto &if_name = p.first;
        auto &if_idx = p.second;

        struct netlink_response resp_real;
        struct getaddr_response *resp = (struct getaddr_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWADDR;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct ifaddrmsg`
        resp->ifaddrmsg = {
            .ifa_family = AF_INET,
            .ifa_prefixlen = 30, // TODO: don't hard code this
            .ifa_flags = IFA_F_PERMANENT,
            .ifa_scope = RT_SCOPE_UNIVERSE,
            .ifa_index = (unsigned int)if_idx
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(IFA_ADDRESS, if_list[if_idx].ipv4_self_addr.s_addr);
        FILL_RTA_INT32(IFA_LOCAL, if_list[if_idx].ipv4_self_addr.s_addr);
        FILL_RTA_STR(IFA_LABEL, if_name);
        FILL_RTA_INT32(IFA_FLAGS, IFA_F_PERMANENT);
        // IFA_CACHEINFO seems to be optional

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
    return 0;
}

/**
 *  RT_SCOPE_LINK, RTN_UNICAST,.rtm_dst_len = popcount(netmask)
 *  for all primary IP (except for loopback devices)
 */
void netlink_fdesc::getroute_main_table(
    const struct nlmsghdr *h,
    int req_seq
)
{
    for (auto &p : if_list) {
        auto &if_idx = p.first;
        auto &interface = p.second;

        if (interface.if_type == IFTYPE_LOOPBACK) {
            continue;
        }

        struct netlink_response resp_real;
        struct getroute_response *resp = (struct getroute_response *)&resp_real;
        struct nlmsghdr *resp_h = &resp->n;
        // set len later
        resp_h->nlmsg_type = RTM_NEWROUTE;
        resp_h->nlmsg_flags = NLM_F_MULTI;
        resp_h->nlmsg_seq = h->nlmsg_seq;
        resp_h->nlmsg_pid = h->nlmsg_pid;

        // fill `struct rtmsg`
        resp->rtmsg = {
            .rtm_family = AF_INET,
            .rtm_dst_len = 30,
            .rtm_src_len = 0,
            .rtm_tos = 0,
            .rtm_table = RT_TABLE_MAIN,
            .rtm_protocol = RTPROT_KERNEL,
            .rtm_scope = RT_SCOPE_LINK,
            .rtm_type = RTN_UNICAST,
            .rtm_flags = 0
        };

        // fill attrs
        int len = sizeof(resp->buf);
        struct rtattr *rta = resp->attrs;
        FILL_RTA_INT32(RTA_TABLE, RT_TABLE_MAIN);
        __u32 addr_h = ntohl(interface.ipv4_self_addr.s_addr);
        __u32 netmask = ~((1ull << (32 - resp->rtmsg.rtm_dst_len)) - 1);
        FILL_RTA_INT32(RTA_DST, htonl(addr_h & netmask));
        FILL_RTA_INT32(RTA_PREFSRC, interface.ipv4_self_addr.s_addr);
        FILL_RTA_INT32(RTA_OIF, if_idx);

        // TODO: msg with dst pointing at entire network and broadcast address
        // TODO: lo msg

        resp_h->nlmsg_len = ((char *)rta - (char *)resp);
        resp_que[req_seq].emplace(resp_real);
    }
}

/**
 * 1. RT_SCOPE_HOST, RTN_LOCAL,.rtm_dst_len = popcount(netmask)
 *      for loopback devices
 * 2. RT_SCOPE_HOST, RTN_LOCAL, rtm_dst_len = 32
 *      for all devices
 * 3. RT_SCOPE_LINK, RTN_BROADCAST, rtm_dst_len = 32
 *      for all devices
 */
void netlink_fdesc::getroute_local_table(
    const struct nlmsghdr *h,
    int req_seq
)
{
    for (auto &p : if_list) {
        auto &if_idx = p.first;
        auto &interface = p.second;

        // 1. loopback routes
        if (interface.if_type == IFTYPE_LOOPBACK) {
            struct netlink_response resp_real;
            struct getroute_response *resp = (struct getroute_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWROUTE;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct rtmsg`
            resp->rtmsg = {
                .rtm_family = AF_INET,
                .rtm_dst_len = 8,
                .rtm_src_len = 0,
                .rtm_tos = 0,
                .rtm_table = RT_TABLE_LOCAL,
                .rtm_protocol = RTPROT_KERNEL,
                .rtm_scope = RT_SCOPE_HOST,
                .rtm_type = RTN_LOCAL,
                .rtm_flags = 0
            };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_INT32(RTA_TABLE, RT_TABLE_LOCAL);
            __u32 addr_h = ntohl(interface.ipv4_self_addr.s_addr);
            __u32 netmask = ~((1ull << (32 - resp->rtmsg.rtm_dst_len)) - 1);
            FILL_RTA_INT32(RTA_DST, htonl(addr_h & netmask));
            FILL_RTA_INT32(RTA_PREFSRC, interface.ipv4_self_addr.s_addr);
            FILL_RTA_INT32(RTA_OIF, if_idx);

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }

        // 2. route to self addr
        {
            struct netlink_response resp_real;
            struct getroute_response *resp = (struct getroute_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWROUTE;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct rtmsg`
            resp->rtmsg = {
                .rtm_family = AF_INET,
                .rtm_dst_len = 32,
                .rtm_src_len = 0,
                .rtm_tos = 0,
                .rtm_table = RT_TABLE_LOCAL,
                .rtm_protocol = RTPROT_KERNEL,
                .rtm_scope = RT_SCOPE_HOST,
                .rtm_type = RTN_LOCAL,
                .rtm_flags = 0
            };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_INT32(RTA_TABLE, RT_TABLE_LOCAL);
            FILL_RTA_INT32(RTA_DST, interface.ipv4_self_addr.s_addr);
            FILL_RTA_INT32(RTA_PREFSRC, interface.ipv4_self_addr.s_addr);
            FILL_RTA_INT32(RTA_OIF, if_idx);

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }

        // 3. route to broadcast addr
        {
            struct netlink_response resp_real;
            struct getroute_response *resp = (struct getroute_response *)&resp_real;
            struct nlmsghdr *resp_h = &resp->n;
            // set len later
            resp_h->nlmsg_type = RTM_NEWROUTE;
            resp_h->nlmsg_flags = NLM_F_MULTI;
            resp_h->nlmsg_seq = h->nlmsg_seq;
            resp_h->nlmsg_pid = h->nlmsg_pid;

            // fill `struct rtmsg`
            resp->rtmsg = {
                .rtm_family = AF_INET,
                .rtm_dst_len = 32,
                .rtm_src_len = 0,
                .rtm_tos = 0,
                .rtm_table = RT_TABLE_LOCAL,
                .rtm_protocol = RTPROT_KERNEL,
                .rtm_scope = RT_SCOPE_LINK,
                .rtm_type = RTN_BROADCAST,
                .rtm_flags = 0
            };

            // fill attrs
            int len = sizeof(resp->buf);
            struct rtattr *rta = resp->attrs;
            FILL_RTA_INT32(RTA_TABLE, RT_TABLE_LOCAL);
            __u32 addr_h = ntohl(interface.ipv4_self_addr.s_addr);
            __u32 netmask = ~((1ull << (32 - resp->rtmsg.rtm_dst_len)) - 1);
            FILL_RTA_INT32(RTA_DST, htonl(addr_h | (~netmask)));
            FILL_RTA_INT32(RTA_PREFSRC, interface.ipv4_self_addr.s_addr);
            FILL_RTA_INT32(RTA_OIF, if_idx);

            resp_h->nlmsg_len = ((char *)rta - (char *)resp);
            resp_que[req_seq].emplace(resp_real);
        }
    }
}

int
netlink_fdesc::handle_getroute_request(
    const struct nlmsghdr *h,
    int req_seq
)
{
    struct rtmsg *req_msg = (struct rtmsg *)NLMSG_DATA(h);
    if (req_msg->rtm_family == AF_INET) {
        // main table
        getroute_main_table(h, req_seq);
        // local table
        getroute_local_table(h, req_seq);
        resp_que[req_seq].emplace(make_nlmsg_done(h));
        return h->nlmsg_len;
    }
    return -1;
}

void netlink_fdesc::poll_netlink()
{
    LOG("poll_netlink() @ fd=%d\n", fd);
    PRELOAD_ORIG(recvfrom);
    PRELOAD_ORIG(recvmsg);

    thread_local static char msgbuf[sizeof(netlink_response) * 2];

    int r;

    while (true) {
        r = recvfrom_orig(fd, NULL, 0, MSG_PEEK|MSG_TRUNC|MSG_DONTWAIT, NULL, NULL);
        if (r <= 0) {
            break;
        }
        struct iovec iovec = {
            .iov_base = msgbuf,
            .iov_len = sizeof(netlink_response)
        };
        struct sockaddr_nl msg_name = (struct sockaddr_nl){
            .nl_family = AF_NETLINK,
            .nl_pid = this->nl_pid,
            .nl_groups = this->nl_groups
        };
        struct msghdr tmp_msghdr = {
            .msg_name = &msg_name,
            .msg_namelen = sizeof(msg_name),
            .msg_iov = &iovec,
            .msg_iovlen = 1,
            .msg_control = NULL,
            .msg_controllen = 0,
            .msg_flags = 0
        };
        int siz = recvmsg_orig(fd, &tmp_msghdr, 0);
        LOG("poll_netlink(): recvmsg_orig() siz=%d, r=%d\n", siz, r);
        debug_assert(siz == r && siz <= sizeof(netlink_response));

        struct nlmsghdr *nlh = (struct nlmsghdr *)msgbuf;
        int remain_len = siz;
        while (remain_len > 0) {
            if (nlh->nlmsg_seq == 0) {
                // push into async msg queue
                async_que.push(*(struct netlink_response *)nlh);
                nlh = NLMSG_NEXT(nlh, remain_len);
                continue;
            }
            // push into ordered response queue
            int useq = kseq_to_useq.at(nlh->nlmsg_seq);
            nlh->nlmsg_seq = useq;
            if (nlh->nlmsg_type == NLMSG_DONE) {
                // inject response here
                if (req_que.at(useq).nlh.nlmsg_type == RTM_GETLINK) {
                    inject_links(&req_que.at(useq).nlh, useq);
                }
                else if (req_que.at(useq).nlh.nlmsg_type == RTM_GETADDR) {
                    inject_addrs(&req_que.at(useq).nlh, useq);
                }
                else if (req_que.at(useq).nlh.nlmsg_type == RTM_GETQDISC) {
                    inject_qdiscs(&req_que.at(useq).nlh, useq);
                }
                LOG("inject done\n");
            }
            // we should create resp_que here, thus operator[]
            resp_que[useq].push(*(struct netlink_response *)nlh);
            nlh = NLMSG_NEXT(nlh, remain_len);
        }
    }
    LOG("poll_netlink() done\n");

    debug_assert(errno == EAGAIN);
    errno = 0;
}

int netlink_fdesc::getsockname(struct sockaddr *addr, socklen_t *addrlen)
{
    struct sockaddr_nl *nladdr = (struct sockaddr_nl *)addr;
    *nladdr = {
        .nl_family = AF_NETLINK,
        .nl_pad = 0,
        .nl_pid = nl_pid,
        .nl_groups = nl_groups
    };
    *addrlen = sizeof(*nladdr);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked NETLINK getsockname(this->fd=%d, addr=%s)=0\n",
        fd, addr_str.c_str());
    return 0;
}


// int netlink_fdesc::accept(struct sockaddr *addr, socklen_t *addrlen, fdesc_set &fdset)
// {
//     assert(0);
// }

ssize_t netlink_fdesc::nl_send_internal(const struct msghdr * msg, const char *buf, int buflen, int flags) {
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    LOG("nl_send_interal(buflen=%d, flags=%x, req_seq=%u)\n", buflen, flags, nlh->nlmsg_seq);
    ssize_t ret = 0;
    bool handled = false;
    int remain_len = buflen;
    while (remain_len > 0) {
        int req_seq = nlh->nlmsg_seq;
        switch (nlh->nlmsg_type) {
        case RTM_GETNEXTHOP:
        {
            ret += handle_getnexthop_request(nlh, req_seq);
            LOG("Hijacked & handled RTM_GETNEXTHOP request @ seq %d\n", nlh->nlmsg_seq);
            req_que.emplace(req_seq, nlh);
            handled = true;
            break;
        }
        case RTM_GETROUTE:
        {
            int r = handle_getroute_request(nlh, req_seq);
            if (r > 0) {
                ret += r;
                LOG("Hijacked & handled RTM_GETROUTE request @ seq %d\n", nlh->nlmsg_seq);
                req_que.emplace(req_seq, nlh);
                handled = true;
            }
            break;
        }
        case RTM_GETVLAN:
        {
            ret += handle_getvlan_request(nlh, req_seq);
            LOG("Hijacked & handled RTM_GETVLAN request @ seq %d\n", nlh->nlmsg_seq);
            req_que.emplace(req_seq, nlh);
            handled = true;
            break;
        }
        case RTM_GETNETCONF:
        {
            ret += handle_getnetconf_request(nlh, req_seq);
            LOG("Hijacked & handled RTM_GETNETCONF request @ seq %d\n", nlh->nlmsg_seq);
            req_que.emplace(req_seq, nlh);
            handled = true;
            break;
        }
        case RTM_GETRULE:
        {
            ret += handle_getrule_request(nlh, req_seq);
            LOG("Hijacked & handled RTM_GETRULE request @ seq %d\n", nlh->nlmsg_seq);
            req_que.emplace(req_seq, nlh);
            handled = true;
            break;
        }
        case RTM_NEWNEXTHOP:
        {
            int r = handle_newnexthop_request(nlh, req_seq);
            if (r > 0) {
                ret += r;
                handled = true;
                LOG("Hijacked & handled RTM_NEWNEXTHOP request @ seq %d\n", nlh->nlmsg_seq);
                req_que.emplace(req_seq, nlh);
            } // else pass to kernel
            break;
        }
        case RTM_DELNEXTHOP:
        {
            int r = handle_delnexthop_request(nlh, req_seq);
            if (r > 0) {
                ret += r;
                handled = true;
                LOG("Hijacked & handled RTM_DELNEXTHOP request @ seq %d\n", nlh->nlmsg_seq);
                req_que.emplace(req_seq, nlh);
            }
            break;
        }
        case RTM_NEWROUTE:
        {
            ret += handle_newroute_request(nlh, req_seq);
            LOG("Hijacked & handled RTM_NEWROUTE request @ seq %d\n", nlh->nlmsg_seq);
            req_que.emplace(req_seq, nlh);
            handled = true;
            break;
        }
        case RTM_GETADDR:
        case RTM_GETLINK:
        case RTM_GETQDISC:
            // pass to kernel, inject later in poll_netlink
            break;
        default:
            LOG("[WARN]: unknown request type %d\n", nlh->nlmsg_type);
            break;
        }
        if (!handled) {
            uint orig_seq = nlh->nlmsg_seq;
            uint kernel_seq = this->nxt_kreq_seq;
            nxt_kreq_seq++;
            kseq_to_useq[kernel_seq] = orig_seq;

            nlh->nlmsg_seq = kernel_seq;

            int r;
            if (msg != nullptr) {
                struct iovec iovec = {
                    .iov_base = nlh,
                    .iov_len = nlh->nlmsg_len
                };
                struct msghdr tmp_msghdr = {
                    .msg_name = msg->msg_name,
                    .msg_namelen = msg->msg_namelen,
                    .msg_iov = &iovec,
                    .msg_iovlen = 1,
                    .msg_control = msg->msg_control,
                    .msg_controllen = msg->msg_controllen,
                    .msg_flags = msg->msg_flags
                };
                PRELOAD_ORIG(sendmsg);
                if ((r = sendmsg_orig(this->fd, &tmp_msghdr, flags)) < 0) {
                    LOG("sendmsg_netlink_impl(): sendmsg_orig(fd=%d, nlmsg_len=%d) failed: %s\n",
                        fd, nlh->nlmsg_len, strerror(errno));
                    nl_dump(nlh, nlh->nlmsg_len);
                    assert(0);
                }
            } else {
                PRELOAD_ORIG(send);
                if ((r = send_orig(this->fd, nlh, nlh->nlmsg_len, flags)) < 0) {
                    LOG("send_netlink_impl(): send_orig(fd=%d, nlmsg_len=%d) failed: %s\n",
                        fd, nlh->nlmsg_len, strerror(errno));
                    nl_dump(nlh, nlh->nlmsg_len);
                    assert(0);
                }
            }
            ret += r;

            nlh->nlmsg_seq = orig_seq;
            req_que.emplace(orig_seq, nlh);
            std::string type_str = pnlmsgtype(nlh->nlmsg_type);
            LOG("Hijacked normal NETLINK request (type %s) and passed to kernel @ seq %u (kernel seq %u)\n",
                type_str.c_str(), orig_seq, kernel_seq);
        }
        if (nlh->nlmsg_type != RTM_NEWLINK) {
            nl_dump(nlh, nlh->nlmsg_len);
        }

        nlh = NLMSG_NEXT(nlh, remain_len);
    }
    return ret;
}

ssize_t netlink_fdesc::send(const void *buf, size_t len, int flags)
{
    LOG("netlink_fdesc::send(fd=%d)\n", this->fd);
    ssize_t ret = nl_send_internal(nullptr, (char *)buf, len, flags);
    LOG("Hijacked NETLINK sendmsg(%d, %p, %x)=%ld\n", this->fd, buf, flags, ret);
    return ret;
}

ssize_t netlink_fdesc::sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    LOG("netlink_fdesc::sendto(fd=%d)\n", this->fd);
    struct iovec iovec = {
        .iov_base = (void *)buf,
        .iov_len = len
    };
    struct msghdr tmp_msghdr = {
        .msg_name = (void *)dest_addr,
        .msg_namelen = addrlen,
        .msg_iov = &iovec,
        .msg_iovlen = 1,
        .msg_control = NULL,
        .msg_controllen = 0,
        .msg_flags = flags
    };
    ssize_t ret = nl_send_internal(&tmp_msghdr, (char *)buf, len, flags);
    std::string addr_str = dest_addr == nullptr ? "nil" : paddr_netlink((const struct sockaddr_nl *)dest_addr);
    LOG("Hijacked NETLINK sendto(fd=%d, buf=%p, flags=%x, addr=%s, addrsiz=%d)=%ld\n",
        this->fd, buf, flags, addr_str.c_str(), addrlen, ret);
    return ret;
}

ssize_t netlink_fdesc::sendmsg(const struct msghdr * msg, int flags)
{
    LOG("netlink_fdesc::sendmsg(fd=%d)\n", this->fd);
    assert(msg->msg_iovlen == 1);
    LOG("msg->msg_iovlen == 1\n");

    ssize_t ret = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
        ret += nl_send_internal(msg, (char *)msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len, flags);
    }
    return ret;
}

ssize_t
netlink_fdesc::recv_impl(
    void *buf, size_t len, int flags)
{
    assert(flags == (MSG_PEEK | MSG_TRUNC) && buf == nullptr);
    int ret;
    poll_netlink();
    if (!async_que.empty()) {
        ret = async_que.front().n.nlmsg_len;
        return ret;
    }
    if (req_que.empty()) {
        errno = EAGAIN;
        ret = -1;
        return ret;
    }
    int curr_seq = req_que.begin()->first;
    if (resp_que.find(curr_seq) == resp_que.end() || resp_que.at(curr_seq).empty()) {
        errno = EAGAIN;
        ret = -1;
        return ret;
    }
    ret = resp_que.at(curr_seq).front().n.nlmsg_len;
    return ret;
}

ssize_t
netlink_fdesc::recv(
    void *buf, size_t len, int flags)
{
    LOG("Entering netlink recv(%d, %p, %ld, %x)\n",
        this->fd, buf, len, flags);
    ssize_t ret = recv_impl(buf, len, flags);
    LOG("Hijacked NETLINK recv(%d, %p, %ld, %x)=%ld\n", this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t
netlink_fdesc::recvfrom(
    void *buf, size_t len, int flags,
    struct sockaddr *src_addr, socklen_t *addrlen)
{
    return this->recv(buf, len, flags);
}

ssize_t
netlink_fdesc::recvmsg_impl(struct msghdr *msg, int flags)
{
    ssize_t ret;

    poll_netlink();

    if (req_que.empty() && async_que.empty()) {  
        LOG("recvmsg_netlink_impl: no message to return.\n");
        errno = EAGAIN;
        ret = -1;
        return ret;
    }
    assert(msg->msg_iovlen == 1);

    struct sockaddr_nl sa = {
        .nl_family = AF_NETLINK,
        .nl_pid = 0,
        .nl_groups = 0
    };
    *(struct sockaddr_nl *)msg->msg_name = sa;

    if (!async_que.empty()) {
        // return from async queue first
        LOG("returning from async queue\n");
        const char *srcbuf = (char *)(&async_que.front());;
        int src_siz = async_que.front().n.nlmsg_len;

        struct nlmsghdr *nlh = (struct nlmsghdr *)srcbuf;
        debug_assert(nlh->nlmsg_seq == 0);

        char *dstbuf = (char *)msg->msg_iov[0].iov_base;
        int dst_remsiz = msg->msg_iov[0].iov_len;
        ret = 0;
        while (!async_que.empty() && dst_remsiz >= src_siz) {
            memmove(dstbuf, srcbuf, src_siz);
            dstbuf += src_siz;
            ret += src_siz;
            dst_remsiz -= src_siz;
            async_que.pop();
            srcbuf = (const char *)(&async_que.front());;
            src_siz = async_que.front().n.nlmsg_len;
            break;
        }
        return ret;
    }

    int curr_seq = req_que.begin()->first;
    if (resp_que.find(curr_seq) == resp_que.end()) {
        LOG("curr_seq %d not found\n", curr_seq);
        errno = EAGAIN;
        ret = -1;
        return ret;
    }
    // resp_que.find(curr_seq) != resp_que.end()
    // face it
    LOG("Hijacked, facing it @ %d\n", this->fd);

#ifdef IMAGE_BIRD
    while (resp_que.at(curr_seq).empty()) {
        poll_netlink();
        struct timespec min_tmo = {
            .tv_sec = 0,
            .tv_nsec = 100'000
        };
        nanosleep(&min_tmo, NULL);
    }
#else
    if (resp_que.at(curr_seq).empty()) {
        LOG("curr_seq %d found but has no response\n", curr_seq);
        errno = EAGAIN;
        ret = -1;
        return ret;
    }
#endif
    char *dstbuf = (char *)msg->msg_iov[0].iov_base;
    int dst_remsiz = msg->msg_iov[0].iov_len;

    const char *srcbuf = (const char *)&resp_que.at(curr_seq).front();
    int src_siz = resp_que.at(curr_seq).front().n.nlmsg_len;
    ret = 0;
    while (dst_remsiz >= src_siz) {
        memmove(dstbuf, srcbuf, src_siz);
        dstbuf += src_siz;
        ret += src_siz;
        dst_remsiz -= src_siz;
        int msgtype = resp_que.at(curr_seq).front().n.nlmsg_type;
        if (msgtype == NLMSG_DONE || msgtype == NLMSG_ERROR) {
            req_que.erase(curr_seq);
        }
        resp_que.at(curr_seq).pop();
        break;
        if (resp_que.at(curr_seq).empty()) {
            resp_que.erase(curr_seq);
            break;
        }
        srcbuf = (const char *)&resp_que.at(curr_seq).front();
        src_siz = resp_que.at(curr_seq).front().n.nlmsg_len;
    }
    return ret;
}

ssize_t
netlink_fdesc::recvmsg(struct msghdr *msg, int flags)
{
    LOG("Entering NETLINK recvmsg(%d, %p, %x)\n", this->fd, msg, flags);
    ssize_t ret = recvmsg_impl(msg, flags);
    LOG("Hijacked NETLINK recvmsg(%d, %p, %x)=%ld\n", this->fd, msg, flags, ret);
    if (ret > 0) {
        int msglen = 0;
        for (int i = 0; i < msg->msg_iovlen; ++i) {
            msglen += msg->msg_iov[i].iov_len;
        }
        LOG("    msg_iovlen=%ld, msglen=%d nl_dump(): \n", msg->msg_iovlen, msglen);
        char *msgbuf = (char *)malloc(msglen); int copied_len = 0;
        for (int i = 0; i < msg->msg_iovlen; ++i) {
            memcpy(msgbuf + copied_len, msg->msg_iov[i].iov_base, msg->msg_iov[i].iov_len);
            copied_len += msg->msg_iov[i].iov_len;
        }
        nl_dump(msgbuf, msglen);
        free(msgbuf);
    }
    return ret;
}

int netlink_fdesc::bind(const struct sockaddr * addr, socklen_t addrlen)
{
    assert(addr->sa_family == AF_NETLINK);
    struct sockaddr_nl *sockaddr = (struct sockaddr_nl *)addr;
    assert(sockaddr->nl_pid == 0);
    this->nl_groups = sockaddr->nl_groups;
    return 0;
}

int netlink_fdesc::listen(int backlog)
{
    PRELOAD_ORIG(listen);
    return listen_orig(this->fd, backlog);
}

bool netlink_fdesc::poll_fastpath(struct pollfd *ufd)
{
    if (!req_que.empty() || !async_que.empty()) {
        // don't pass the fd to kernel, we can handle it
        ufd->revents = ufd->events & (POLLIN | POLLOUT);
        return true;
    }
    return false;
}

void netlink_fdesc::poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd)
{
    uint32_t revents = kfd->revents;
    // POLLIN
    if (kfd->events & POLLIN) {
        poll_netlink();
        if ((!req_que.empty() || !async_que.empty())) {
            revents |= POLLIN;
        }
    }
    // POLLOUT
    if (kfd->events & POLLOUT) {
        if (!(kfd->revents & POLLOUT)) {
            fprintf(stderr, "[WARN] ppoll netlink fd (%d) for POLLOUT failed\n", fd);
        }
        revents |= POLLOUT;
    }
    // errors are already contained in revents
    ufd->revents = revents;
}

int
netlink_fdesc::getsockopt(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    PRELOAD_ORIG(getsockopt);
    int ret = getsockopt_orig(this->fd, level, optname, optval, optlen);
    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Hijacked NETLINK getsockopt(%d, %s, %s, %u)=%d\n",
        this->fd, level_str.c_str(),
        optname_str.c_str(),
        *optlen, ret);
    return ret;
}
