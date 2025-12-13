#ifndef UTIL_H
#define UTIL_H

#include "debug.h"
#include <string>
#include <arpa/inet.h>

static void
log_backtrace(void)
{
#define BT_BUF_SIZE 32
    int j, nptrs;
    void *buffer[BT_BUF_SIZE];
    char **strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);

    /* The call backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO)
        would produce similar output to the following: */

    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }

    LOG("--- backtrace ---\n");
    for (j = 0; j < nptrs; j++)
        LOG("Frame %d: %s [@%p]\n", j, strings[j], buffer[j]);

    free(strings);
}

static std::string
str2hex(const char *str, size_t len)
{
    if (str == NULL) {
        return "{nullptr}";
    }
    std::string ret;
    static char dict[17] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        ret += "\\x" + std::string{dict[(str[i] & 0xF0) >> 4]} + std::string{dict[str[i] & 0xF]};
    }
    return ret;
}

static std::string
hex2str(unsigned long num)
{
    static char hexc[] = "0123456789ABCDEF";
    std::string str;
    while (num) {
        str += hexc[num % 16];
        num /= 16;
    }
    str += "x0";
    std::reverse(str.begin(), str.end());
    return str;
}

static std::string
pioctl_req(int req)
{
    std::string str;
    switch (req) {
    PCASEB(SIOCETHTOOL)
    default:
        str += "UNKNOWN_REQ(" + hex2str(req) + ")";
        break;
    }
    return str;
}

static std::string
pfcntl_cmd(int cmd)
{
    std::string str;
    switch (cmd) {
    PCASEB(F_DUPFD)
    PCASEB(F_GETFD)
    PCASEB(F_SETFD)
    PCASEB(F_GETFL)
    PCASEB(F_SETFL)
    PCASEB(F_GETLK)
    PCASEB(F_SETLK)
    default:
        str += "UNKNOWN_CMD(" + std::to_string(cmd) + ")";
        break;
    }
    return str;
}

static std::string
psock_domain(int domain)
{
    std::string str;
    switch (domain) {
    PCASEB(AF_UNIX)
    // PCASEB(AF_LOCAL)
    PCASEB(AF_INET)
    PCASEB(AF_AX25)
    PCASEB(AF_IPX)
    PCASEB(AF_APPLETALK)
    PCASEB(AF_X25)
    PCASEB(AF_INET6)
    PCASEB(AF_DECnet)
    PCASEB(AF_KEY)
    PCASEB(AF_NETLINK)
    PCASEB(AF_PACKET)
    PCASEB(AF_RDS)
    PCASEB(AF_PPPOX)
    PCASEB(AF_LLC)
    PCASEB(AF_IB)
    PCASEB(AF_MPLS)
    PCASEB(AF_CAN)
    PCASEB(AF_TIPC)
    PCASEB(AF_BLUETOOTH)
    PCASEB(AF_ALG)
    PCASEB(AF_VSOCK)
    PCASEB(AF_KCM)
    PCASEB(AF_XDP)
    default:
        str += "AF_UNKNOWN(" + std::to_string(domain) + ")"; break;
    }
    return str;
}

static std::string
psock_type(int type)
{
    std::string str;
    if (type & SOCK_NONBLOCK) {
        str += "SOCK_NONBLOCK | ";
    }
    if (type & SOCK_CLOEXEC) {
        str += "SOCK_CLOEXEC | ";
    }
    type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    switch (type) {
    PCASEB(SOCK_STREAM)
    PCASEB(SOCK_DGRAM)
    PCASEB(SOCK_SEQPACKET)
    PCASEB(SOCK_RAW)
    PCASEB(SOCK_RDM)
    PCASEB(SOCK_PACKET)
    default:
        str += "SOCK_UNKNOWN(" + std::to_string(type) + ")"; break;
    }
    return str;
}

static std::string
paddr_netlink(const struct sockaddr_nl *nladdr)
{
    std::string str = "nl_family: ";
    switch (nladdr->nl_family) {
    PCASEB(AF_NETLINK)
    default:
        str += "unknown ul_family(" + std::to_string(nladdr->nl_family) + ")";
    }
    str += ", nl_pid: " + std::to_string(nladdr->nl_pid) + ", nl_groups: " + std::to_string(nladdr->nl_groups);
    return str;
}

static std::string
paddr_inet(const struct sockaddr_in *addr)
{
    std::string str;
    static char buf[INET_ADDRSTRLEN + 5];
    inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
    str += std::string(buf) + ":" + std::to_string(ntohs(addr->sin_port));
    return str;
}

static std::string
paddr_inet6(const struct sockaddr_in6 *addr)
{
    std::string str;
    static char buf[INET6_ADDRSTRLEN + 5];
    inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf));
    str += std::string(buf) + ":" + std::to_string(ntohs(addr->sin6_port));
    return str;
}

static std::string
paddr_unix(const struct sockaddr_un *addr)
{
    std::string str;
    str += std::string(addr->sun_path);
    return str;
}

static std::string
paddr(const struct sockaddr *addr, const socklen_t addrlen)
{
    if (addr == NULL) {
        return "{nullptr}";
    }
    switch (addr->sa_family) {
    case AF_INET:
        return paddr_inet((const sockaddr_in *)addr);
    case AF_NETLINK:
        return paddr_netlink((sockaddr_nl *)addr);
    case AF_INET6:
        return paddr_inet6((const sockaddr_in6 *)addr);
    case AF_UNIX:
        return paddr_unix((const sockaddr_un *)addr);
    default:
        return std::string("{") + "UNKNOWN family:" + psock_domain(addr->sa_family) + str2hex(addr->sa_data, addrlen) +  "}";
    }
}

static std::string
paddrinfo(const struct addrinfo *ai)
{
    if (ai == NULL) {
        return "{nullptr}";
    }
    
    std::stringstream ret;
    ret << "struct addrinfo {";
    ret << ".ai_flags=" << ai->ai_flags << ", ";
    ret << ".ai_family=" << ai->ai_family << ", ";
    ret << ".ai_socktype=" << ai->ai_socktype << ", ";
    ret << ".ai_protocol=" << ai->ai_protocol << ", ";
    ret << ".ai_addrlen=" << ai->ai_addrlen << ", ";
    ret << ".ai_addr=" << paddr(ai->ai_addr, ai->ai_addrlen) << ", ";
    ret << ".ai_canonname=" << (ai->ai_canonname == nullptr ? "nullptr" : ai->ai_canonname) << " (" << __u64(ai->ai_canonname) << ") " << ", ";
    ret << ".ai_next=" << (void *)ai->ai_next << "";
    ret << "}";
    return ret.str();
}

static std::string
pnlsock_family(int type)
{
    std::string str;
    switch (type) {
    PCASEB(NETLINK_ROUTE)
    PCASEB(NETLINK_GENERIC)
    default:
        str += "NETLINK_UNKNOWN(" + std::to_string(type) + ")"; break;
    }
    return str;
}

static std::string
pevents(short events)
{
    std::string str;
    bool use_or = false;
#define PEVT(evt) \
    if (events & evt) { \
        str += std::string(use_or ? " | " : "") + #evt; \
        use_or = true; \
    }
    PEVT(POLLIN);
    PEVT(POLLPRI);
    PEVT(POLLOUT);
    PEVT(POLLRDHUP);
    PEVT(POLLERR);
    PEVT(POLLHUP);
    PEVT(POLLNVAL);
    PEVT(POLLRDBAND);
    PEVT(POLLWRBAND);
    return str;
}

static std::string
pnlmsgtype(int type)
{
    std::string str;
    switch (type) {
    PCASEB(RTM_NEWLINK)
    PCASEB(RTM_DELLINK)
    PCASEB(RTM_GETLINK)
    PCASEB(RTM_SETLINK)

    PCASEB(RTM_NEWADDR)
    PCASEB(RTM_DELADDR)
    PCASEB(RTM_GETADDR)

    PCASEB(RTM_NEWROUTE)
    PCASEB(RTM_DELROUTE)
    PCASEB(RTM_GETROUTE)

    PCASEB(RTM_NEWNEIGH)
    PCASEB(RTM_DELNEIGH)
    PCASEB(RTM_GETNEIGH)

    PCASEB(RTM_NEWRULE)
    PCASEB(RTM_DELRULE)
    PCASEB(RTM_GETRULE)

    PCASEB(RTM_NEWQDISC)
    PCASEB(RTM_DELQDISC)
    PCASEB(RTM_GETQDISC)

    PCASEB(RTM_NEWTCLASS)
    PCASEB(RTM_DELTCLASS)
    PCASEB(RTM_GETTCLASS)

    PCASEB(RTM_NEWTFILTER)
    PCASEB(RTM_DELTFILTER)
    PCASEB(RTM_GETTFILTER)

    PCASEB(RTM_NEWACTION)
    PCASEB(RTM_DELACTION)
    PCASEB(RTM_GETACTION)

    PCASEB(RTM_NEWPREFIX)

    PCASEB(RTM_GETMULTICAST)

    PCASEB(RTM_GETANYCAST)

    PCASEB(RTM_NEWNEIGHTBL)
    PCASEB(RTM_GETNEIGHTBL)
    PCASEB(RTM_SETNEIGHTBL)

    PCASEB(RTM_NEWNDUSEROPT)

    PCASEB(RTM_NEWADDRLABEL)
    PCASEB(RTM_DELADDRLABEL)
    PCASEB(RTM_GETADDRLABEL)

    PCASEB(RTM_GETDCB)
    PCASEB(RTM_SETDCB)

    PCASEB(RTM_NEWNETCONF)
    PCASEB(RTM_DELNETCONF)
    PCASEB(RTM_GETNETCONF)

    PCASEB(RTM_NEWMDB)
    PCASEB(RTM_DELMDB)
    PCASEB(RTM_GETMDB)

    PCASEB(RTM_NEWNSID)
    PCASEB(RTM_DELNSID)
    PCASEB(RTM_GETNSID)

    PCASEB(RTM_NEWSTATS)
    PCASEB(RTM_GETSTATS)

    PCASEB(RTM_NEWCACHEREPORT)

    PCASEB(RTM_NEWCHAIN)
    PCASEB(RTM_DELCHAIN)
    PCASEB(RTM_GETCHAIN)

    PCASEB(RTM_NEWNEXTHOP)
    PCASEB(RTM_DELNEXTHOP)
    PCASEB(RTM_GETNEXTHOP)
    default:
        str = "UNKNOWN netlink request type (" + std::to_string(type) + ")";
    }
    return str;
};


static std::string
psockopt_level(int level)
{
    std::string str;
    switch (level) {
    PCASEB(SOL_SOCKET)
    PCASEB(SOL_NETLINK)
    PCASEB(IPPROTO_TCP)
    PCASEB(IPPROTO_UDP)
    PCASEB(IPPROTO_IP)
    PCASEB(IPPROTO_IPV6)
    PCASEB(IPPROTO_RAW)
    default:
        str += "SOL_UNKNOWN(" + std::to_string(level) + ")"; break;
    }
    return str;
}

static std::string
ip_optname_str(int optname, const void *optval)
{
    std::string str;
    switch (optname) {
    PCASE(IP_TOS)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASE(IP_TTL)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASEB(IP_HDRINCL)
    PCASEB(IP_OPTIONS)
    PCASEB(IP_ROUTER_ALERT)
    PCASEB(IP_RECVOPTS)
    PCASEB(IP_RETOPTS)
    PCASEB(IP_PKTINFO)
    PCASEB(IP_PKTOPTIONS)
    PCASEB(IP_MTU_DISCOVER)
    PCASEB(IP_RECVERR)
    PCASEB(IP_RECVTTL)
    PCASEB(IP_RECVTOS)
    PCASEB(IP_MTU)
    PCASEB(IP_FREEBIND)
    PCASEB(IP_IPSEC_POLICY)
    PCASEB(IP_XFRM_POLICY)
    PCASEB(IP_PASSSEC)
    PCASEB(IP_TRANSPARENT)
    default:
        str += "IP_UNKNOWN(" + std::to_string(optname) + ")"; break;
    }
    return str;
}

static std::string
tcp_optname_str(int optname, const void *optval)
{
    std::string str;
    switch (optname) {
    PCASEB(TCP_NODELAY)
    PCASEB(TCP_MAXSEG)
    PCASEB(TCP_CORK)
    PCASEB(TCP_KEEPIDLE)
    PCASEB(TCP_KEEPINTVL)
    PCASEB(TCP_KEEPCNT)
    PCASEB(TCP_SYNCNT)
    PCASEB(TCP_LINGER2)
    PCASEB(TCP_DEFER_ACCEPT)
    PCASEB(TCP_WINDOW_CLAMP)
    PCASE(TCP_INFO)
        {
            const struct tcp_info *ti = (const struct tcp_info *)optval;
            // only rtt is in use
            str += "{tcpi_rtt=" + std::to_string(ti->tcpi_rtt) + "}";
        }
        break;
    PCASEB(TCP_QUICKACK)
    PCASEB(TCP_CONGESTION)
    PCASEB(TCP_MD5SIG)
    PCASEB(TCP_COOKIE_TRANSACTIONS)
    PCASEB(TCP_THIN_LINEAR_TIMEOUTS)
    PCASEB(TCP_THIN_DUPACK)
    PCASEB(TCP_USER_TIMEOUT)
    PCASEB(TCP_REPAIR)
    PCASEB(TCP_REPAIR_QUEUE)
    PCASEB(TCP_QUEUE_SEQ)
    PCASEB(TCP_REPAIR_OPTIONS)
    PCASEB(TCP_FASTOPEN)
    PCASEB(TCP_TIMESTAMP)
    PCASEB(TCP_NOTSENT_LOWAT)
    PCASEB(TCP_CC_INFO)
    PCASEB(TCP_SAVE_SYN)
    PCASEB(TCP_SAVED_SYN)
    PCASEB(TCP_REPAIR_WINDOW)
    PCASEB(TCP_FASTOPEN_CONNECT)
    PCASEB(TCP_ULP)
    PCASEB(TCP_MD5SIG_EXT)
    PCASEB(TCP_FASTOPEN_KEY)
    PCASEB(TCP_FASTOPEN_NO_COOKIE)
    PCASEB(TCP_ZEROCOPY_RECEIVE)
    PCASEB(TCP_INQ)
    PCASEB(TCP_TX_DELAY)
    default:
        str += "TCP_UNKNOWN(" + std::to_string(optname) + ")"; break;
    }
    return str;
}

static std::string
sock_optname_str(int optname, const void *optval)
{
    std::string str;
    switch (optname) {
    PCASEB(SO_DEBUG)
    PCASE(SO_REUSEADDR)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASEB(SO_TYPE)
    PCASE(SO_ERROR)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASEB(SO_DONTROUTE)
    PCASEB(SO_BROADCAST)
    PCASE(SO_SNDBUF)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASE(SO_RCVBUF)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASE(SO_SNDBUFFORCE)
        str += "=" + std::to_string(*(int *)optval);;
        break;
    PCASE(SO_RCVBUFFORCE)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASEB(SO_KEEPALIVE)
    PCASEB(SO_OOBINLINE)
    PCASEB(SO_NO_CHECK)
    PCASEB(SO_PRIORITY)
    PCASEB(SO_LINGER)
    PCASEB(SO_BSDCOMPAT)
    PCASE(SO_REUSEPORT)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASEB(SO_PASSCRED)
    PCASEB(SO_PEERCRED)
    PCASEB(SO_RCVLOWAT)
    PCASEB(SO_SNDLOWAT)
    PCASEB(SO_RCVTIMEO)
    PCASEB(SO_SNDTIMEO)
    PCASE(SO_BINDTODEVICE)
        str += "=" + std::string((char *)optval);
        break;
    PCASEB(SO_ATTACH_FILTER)
    PCASEB(SO_PEERNAME)
    PCASEB(SO_ACCEPTCONN)
    PCASEB(SO_PROTOCOL)
    PCASEB(SO_DOMAIN)
    PCASEB(SO_BUSY_POLL)
    default:
        str += "SO_UNKNOWN(" + std::to_string(optname) + ")"; break;
    }
    return str;
}

static std::string
netlink_optname_str(int optname, const void *optval)
{
    std::string str;
    switch (optname) {
    PCASE(SO_RCVBUF)
        str += "=" + std::to_string(*(int *)optval);
        break;
    PCASE(SO_RCVBUFFORCE)
        str += "=" + std::to_string(*(int *)optval);
        break;
    default:
        str += "SO_UNKNOWN(" + std::to_string(optname) + ")"; break;
    }
    return str;
}

static std::string
psockopt_optname(int level, int optname, const void *optval)
{
    char *buf;
    switch(level) {
    case SOL_SOCKET:
        return sock_optname_str(optname, optval);
    case SOL_NETLINK:
        return netlink_optname_str(optname, optval);
    case IPPROTO_IP:
        return ip_optname_str(optname, optval);
    case IPPROTO_TCP:
        return tcp_optname_str(optname, optval);
    default:
        return "OPTNAME_UNKNOWN(" + std::to_string(optname) + ")";
    }
}

#endif