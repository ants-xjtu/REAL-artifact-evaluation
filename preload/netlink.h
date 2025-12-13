#pragma once

#include <vector>
#include <queue>
#include <map>
#include <string>
#include <shared_mutex>
#include <thread>
#include "fdesc.h"

extern "C" {

#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/rtnetlink.h>
#include <linux/nexthop.h>
#include <linux/netconf.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <linux/if_addr.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if_arp.h>
#include <sys/types.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/snmp.h>
#include <linux/fib_rules.h>

}

// TODO: this is dirty, maybe compile in a docker container
// to fix the linux header issue
// enum {
// 	RTM_NEWVLAN = 112,
// #define RTM_NEWNVLAN	RTM_NEWVLAN
// 	RTM_DELVLAN,
// #define RTM_DELVLAN	RTM_DELVLAN
// 	RTM_GETVLAN,
// #define RTM_GETVLAN	RTM_GETVLAN

// 	RTM_NEWNEXTHOPBUCKET = 116,
// #define RTM_NEWNEXTHOPBUCKET	RTM_NEWNEXTHOPBUCKET
// 	RTM_DELNEXTHOPBUCKET,
// #define RTM_DELNEXTHOPBUCKET	RTM_DELNEXTHOPBUCKET
// 	RTM_GETNEXTHOPBUCKET,
// #define RTM_GETNEXTHOPBUCKET	RTM_GETNEXTHOPBUCKET

// 	__RTM_MAX,
// #define RTM_MAX		(((__RTM_MAX + 3) & ~3) - 1)
// };

class netlink_fdset;

struct netlink_response {
    struct nlmsghdr n;
    char buf[4096 - sizeof(struct nlmsghdr)];
};

struct getlink_response {
    struct nlmsghdr n;
    struct ifinfomsg ifinfomsg;
    char padding[NLMSG_ALIGN(sizeof(struct ifinfomsg)) - sizeof(struct ifinfomsg)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct getnetconf_response {
    struct nlmsghdr n;
    struct netconfmsg netconfmsg;
    char padding[NLMSG_ALIGN(sizeof(struct netconfmsg)) - sizeof(struct netconfmsg)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct getaddr_response {
    struct nlmsghdr n;
    struct ifaddrmsg ifaddrmsg;
    char padding[NLMSG_ALIGN(sizeof(struct ifaddrmsg)) - sizeof(struct ifaddrmsg)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct getroute_response {
    struct nlmsghdr n;
    struct rtmsg rtmsg;
    char padding[NLMSG_ALIGN(sizeof(struct rtmsg)) - sizeof(struct rtmsg)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct getrule_response {
    struct nlmsghdr n;
    struct fib_rule_hdr rulemsg;
    char padding[NLMSG_ALIGN(sizeof(struct fib_rule_hdr)) - sizeof(struct fib_rule_hdr)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct getqdisc_response {
    struct nlmsghdr n;
    struct tcmsg tcmsg;
    char padding[NLMSG_ALIGN(sizeof(struct tcmsg)) - sizeof(struct tcmsg)];
    struct rtattr attrs[0];
    char buf[1500];
};

struct nl_pending_req {
    struct nlmsghdr nlh;
    nl_pending_req() = delete;
    nl_pending_req(const struct nlmsghdr *h): nlh(*h) {}
};

enum IFTYPE {
    IFTYPE_LOOPBACK = 1,
    IFTYPE_VETH,
};

struct hwaddr {
    __u8 hw_addr_len;
    __u8 hw_addr[20];
};

void
add_if(
    int idx,
    std::string ifname,
    IFTYPE type,
    struct in_addr sip,
    struct in_addr pip,
    hwaddr self_mac,
    hwaddr broadcast_mac
);

struct netif {
    int idx;
    std::string name;
    IFTYPE if_type;
    struct in_addr ipv4_self_addr;
    struct in_addr ipv4_peer_addr;
    int prefix_len;
    struct ifinfomsg ifinfo;
    // {
    //     __u8	ifi_family; /* both: AF_UNSPEC*/
    //     __u8	__ifi_pad;
    //     __u16	ifi_type;		/* ARPHRD_* */
    //                             /* lo: ARPHRD_LOOPBACK */
    //                             /* eth: ARPHRD_ETHER */
    //     // __s32	ifi_index;		/* Link index	*/
    //     __u32	ifi_flags;		/* IFF_* flags	*/
    //                             /* lo: IFF_UP | IFF_LOOPBACK | IFF_RUNNING | IFF_LOWER_UP */
    //                             /* eth: IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST | IFF_LOWER_UP */
    //     __u32	ifi_change;		/* IFF_* change mask */
    //                             /* both: 0*/
    // }
    struct ifla {
        // std::string ifname;
        __u32	txqlen; // both: 1000
        __u8	operstate; // 0 for lo, 6 for eth
        __u8	linkmode; // both: 0
        __u32	mtu; // lo: 65536, eth: 1500
        __u32	min_mtu; // lo: 0, eth: 68
        __u32	max_mtu; // lo: 0, eth: 65535
        __u32	group; // both: 0
        __u32	promiscuity; // both: 0
        __u32	num_tx_queues; // lo: 1, eth: 24
        __u32	gso_max_segs; // both: 65535
        __u32	gso_max_size; // both: 65536
        __u32	num_rx_queues; // lo: 1, eth: 24
        __u8	carrier; // both: 1
        std::string	qdisc; // both: "noqueue"
        __u32	carrier_changes; // lo: 0, eth: 2
        __u32	carrier_up_count; // lo: 0, eth: 1
        __u32	carrier_down_count; // lo: 0, eth: 1
        __u8	proto_down; // both: 0
        struct {
            __u64	mem_start;
            __u64	mem_end;
            __u64	base_addr;
            __u16	irq;
            __u8	dma;
            __u8	port;
        } ifmap; // both: all zero
        hwaddr hw_addr; // lo: all zero, eth: MAC address
        hwaddr hw_broadcast; // lo: all zero, eth: all one (ff: ff: ff: ff: ff: ff)
        struct {
            __u64	rx_packets;		/* total packets received	*/
            __u64	tx_packets;		/* total packets transmitted	*/
            __u64	rx_bytes;		/* total bytes received 	*/
            __u64	tx_bytes;		/* total bytes transmitted	*/
            __u64	rx_errors;		/* bad packets received		*/
            __u64	tx_errors;		/* packet transmit problems	*/
            __u64	rx_dropped;		/* no space in linux buffers	*/
            __u64	tx_dropped;		/* no space available in linux	*/
            __u64	multicast;		/* multicast packets received	*/
            __u64	collisions;

            /* detailed rx_errors: */
            __u64	rx_length_errors;
            __u64	rx_over_errors;		/* receiver ring buff overflow	*/
            __u64	rx_crc_errors;		/* recved pkt with crc error	*/
            __u64	rx_frame_errors;	/* recv'd frame alignment error */
            __u64	rx_fifo_errors;		/* recv'r fifo overrun		*/
            __u64	rx_missed_errors;	/* receiver missed packet	*/

            /* detailed tx_errors */
            __u64	tx_aborted_errors;
            __u64	tx_carrier_errors;
            __u64	tx_fifo_errors;
            __u64	tx_heartbeat_errors;
            __u64	tx_window_errors;

            /* for cslip etc */
            __u64	rx_compressed;
            __u64	tx_compressed;

            __u64	rx_nohandler;		/* dropped, no handler found	*/
        } stats64 ; // lo: all zero, eth: real stats
        struct {
            __u32	rx_packets;		/* total packets received	*/
            __u32	tx_packets;		/* total packets transmitted	*/
            __u32	rx_bytes;		/* total bytes received 	*/
            __u32	tx_bytes;		/* total bytes transmitted	*/
            __u32	rx_errors;		/* bad packets received		*/
            __u32	tx_errors;		/* packet transmit problems	*/
            __u32	rx_dropped;		/* no space in linux buffers	*/
            __u32	tx_dropped;		/* no space available in linux	*/
            __u32	multicast;		/* multicast packets received	*/
            __u32	collisions;

            /* detailed rx_errors: */
            __u32	rx_length_errors;
            __u32	rx_over_errors;		/* receiver ring buff overflow	*/
            __u32	rx_crc_errors;		/* recved pkt with crc error	*/
            __u32	rx_frame_errors;	/* recv'd frame alignment error */
            __u32	rx_fifo_errors;		/* recv'r fifo overrun		*/
            __u32	rx_missed_errors;	/* receiver missed packet	*/

            /* detailed tx_errors */
            __u32	tx_aborted_errors;
            __u32	tx_carrier_errors;
            __u32	tx_fifo_errors;
            __u32	tx_heartbeat_errors;
            __u32	tx_window_errors;

            /* for cslip etc */
            __u32	rx_compressed;
            __u32	tx_compressed;

            __u32	rx_nohandler;		/* dropped, no handler found	*/
        } stats; // lo: all zero, eth: real stats, sync with stats64
        struct {
            __u8	xdp_attached;
        } xdp; // both: XDP_ATTACHED_NONE
        struct {
            std::string info_kind;
        } linkinfo; // eth only: "veth"
        __u32	link_netnsid; // eth only: 0
        /* IFLA_LINK.
            For usual devices it is equal ifi_index.
            If it is a "virtual interface" (f.e. tunnel), ifi_link
            can point to real physical interface (f.e. for bandwidth calculations),
            or maybe 0, what means, that real media is unknown (usual
            for IPIP tunnels, when route to endpoint is allowed to change)
            */
        __u32	link; // eth only: ifi_index

        // seems that frr only care about VLAN-related things in AF_SPEC,
        // since we don't use VLAN, we fill IFLA_AF_SPEC with dumb value
        struct {
            struct {
                __u32	inet_conf[__IPV4_DEVCONF_MAX];
            } af_inet;
            struct {
                __u32	inet6_flags;\
                struct {
                    __u32	max_reasm_len;
                    __u32	tstamp;		/* ipv6InterfaceTable updated timestamp */
                    __u32	reachable_time;
                    __u32	retrans_time;
                } inet6_cacheinfo;
                __u32	inet6_conf[DEVCONF_MAX];
                __u64	inet6_stats[__IPSTATS_MIB_MAX];
                __u64	inet6_icmp6stats[__ICMP6_MIB_MAX];
                struct in6_addr	inet6_token;
                __u8	inet6_addr_gen_mode;
            } af_inet6;
        } af_spec;
    } ifla;
};

class netlink_fdesc : public fdesc {
public:
    netlink_fdesc(int _fd, int _nl_pid, int _nl_groups, int _nxt_kreq_seq)
        : fdesc(_fd, FDESC_NETLINK), nl_pid(_nl_pid), nl_groups(_nl_groups), nxt_kreq_seq(_nxt_kreq_seq) {}
    int getsockname(struct sockaddr *addr, socklen_t *addrlen) override;
    // int accept(struct sockaddr *addr, socklen_t *addrlen, fdesc_set &fdset) override;
    ssize_t send(const void *buf, size_t len, int flags) override;
    ssize_t sendmsg(const struct msghdr * msg, int flags) override;
    ssize_t sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) override;
    ssize_t recv(void *buf, size_t len, int flags) override;
    ssize_t recvfrom(
        void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen) override;
    ssize_t recvmsg(struct msghdr *msg, int flags) override;
    int bind(const struct sockaddr * addr, socklen_t addrlen) override;
    bool poll_fastpath(struct pollfd *ufd) override; // returns skip original poll or not
    void poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd) override;
    int getsockopt(int level, int optname, void *optval, socklen_t *optlen) override;
    int listen(int backlog) override;

private:
    __u32 nl_pid;
    __u32 nl_groups;
    int nxt_kreq_seq;
    std::map<int, nl_pending_req> req_que; // seq -> [request type]
    std::map<int, std::queue<netlink_response>> resp_que; // seq -> [response]
    std::queue<netlink_response> async_que;
    std::map<uint, uint> kseq_to_useq;

    ssize_t recvmsg_impl(struct msghdr *msg, int flags);
    ssize_t recv_impl(void *buf, size_t len, int flags);
    void poll_netlink();

    ssize_t nl_send_internal(const struct msghdr *msg, const char *buf, int buflen, int flags);

    int
    inject_links(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_getnetconf_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_getnexthop_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_getvlan_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_getrule_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    inject_qdiscs(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_newnexthop_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_delnexthop_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_newroute_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    inject_addrs(
        const struct nlmsghdr *h,
        int req_seq
    );

    int
    handle_getroute_request(
        const struct nlmsghdr *h,
        int req_seq
    );

    void getroute_main_table(
        const struct nlmsghdr *h,
        int req_seq
    );

    void getroute_local_table(
        const struct nlmsghdr *h,
        int req_seq
    );

    void getrule_inet(
        const struct nlmsghdr *h,
        int req_seq
    );

    void getrule_inet6(
        const struct nlmsghdr *h,
        int req_seq
    );
};
