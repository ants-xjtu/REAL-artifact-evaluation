#include "udp.h"
#include "preload.h"
#include <set>

ssize_t udp_fdesc::write(const void *buf, size_t count)
{
    PRELOAD_ORIG(write);
    ssize_t ret = write_orig(this->fd, buf, count);
    LOG("Hijacked UDP write(%d, %p, %ld)=%ld\n", this->fd, buf, count, ret);
    return ret;
}

ssize_t udp_fdesc::writev(const struct iovec *iov, int iovcnt)
{
    PRELOAD_ORIG(writev);
    ssize_t ret = writev_orig(this->fd, iov, iovcnt);
    LOG("Hijacked UDP writev(%d, %p, %d)=%ld\n", this->fd, iov, iovcnt, ret);
    return ret;
}

ssize_t udp_fdesc::read(void *buf, size_t count)
{
    PRELOAD_ORIG(read);
    ssize_t ret = read_orig(this->fd, buf, count);
    LOG("Hijacked UDP read(%d, %p, %ld)=%ld\n", this->fd, buf, count, ret);
    return ret;
}

int udp_fdesc::getsockname(struct sockaddr *addr, socklen_t *addrlen)
{
    struct sockaddr_in *inaddr = (struct sockaddr_in *)addr;
    *inaddr = {
        .sin_family = AF_INET,
        .sin_port = self_port,
        .sin_addr = {self_addr}
    };
    *addrlen = sizeof(*inaddr);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked UDP getsockname(sockfd=%d, addr=%s)=0\n",
        fd, addr_str.c_str());
    return 0;
}

int udp_fdesc::getpeername(struct sockaddr *addr, socklen_t *addrlen)
{
    struct sockaddr_in *inaddr = (struct sockaddr_in *)addr;
    *inaddr = {
        .sin_family = AF_INET,
        .sin_port = peer_port,
        .sin_addr = {peer_addr}
    };
    *addrlen = sizeof(*inaddr);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked TCP getpeername(sockfd=%d, addr=%s)=0\n",
        fd, addr_str.c_str());
    return 0;
}

int udp_fdesc::connect(const struct sockaddr *addr, socklen_t addrlen)
{
    PRELOAD_ORIG(connect);
    PRELOAD_ORIG(bind);
    const struct sockaddr_in *in_addr = ((const struct sockaddr_in *)addr);
    int ret;

    // 1. prepare IPv4 address
    int peer_id;
    struct sockaddr_in localhost;
    inet_aton("127.0.0.1", &localhost.sin_addr);
    if (in_addr->sin_addr.s_addr == localhost.sin_addr.s_addr) {
        peer_id = tls_selfid;
    } else {
        peer_id = tls_pip2peerid[in_addr->sin_addr.s_addr];
    }
    in_port_t curr_port = (in_port_t)htons(get_port());
    struct sockaddr_in inet_srcaddr = {.sin_family = AF_INET, .sin_port = curr_port, .sin_addr = {tls_peers[peer_id].self_addr}};

    /* disable the limitation below, as graceful restart only allows
    connection in one direction, i.e. restarted server to its peer*/
    // if (peer_id < selfid) {
    //     // only allow connection from low to high
    //     // but allow self to self
    //     errno = ECONNREFUSED;
    //     return -1;
    // }

    // 2. bind the unix domain socket according to IPv4 addr
    struct sockaddr_un uds_srcaddr = {.sun_family = AF_UNIX };
    std::string addr_str = paddr_inet(&inet_srcaddr);
    sprintf(uds_srcaddr.sun_path, "/ripc/emu-real-%d/%s", tls_selfid, addr_str.c_str());
    ret = unlink(uds_srcaddr.sun_path);
    if (ret < 0 && errno != ENOENT) {
        fprintf(stderr, "connect_impl(): unlink [%s] before bind failed: %s", uds_srcaddr.sun_path, strerror(errno));
        assert(0);
    }
    ret = bind_orig(this->fd, (struct sockaddr *)&uds_srcaddr, sizeof(uds_srcaddr));
    if (ret < 0) {
        LOG("connect_impl(): bind(%d, addr=%s) for binding client to path failed: %s\n",
            this->fd, uds_srcaddr.sun_path, strerror(errno));
    }

    // 3. call original connect
    struct sockaddr_un uds_dstaddr = {.sun_family = AF_UNIX };
    sprintf(uds_dstaddr.sun_path, "/ripc/emu-real-%d/listener:%hu", peer_id, ntohs(in_addr->sin_port));
    ret = connect_orig(this->fd, (const struct sockaddr *)&uds_dstaddr, sizeof(uds_dstaddr));

    if (ret < 0 && errno == ENOENT) {
        errno = ECONNREFUSED;
    }

    this->peer_id = peer_id;
    this->peer_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
    this->peer_port =((struct sockaddr_in *)addr)->sin_port;
    this->self_addr = tls_peers[peer_id].self_addr;
    this->self_port = curr_port;

    addr_str = paddr_inet((struct sockaddr_in *)addr);
    LOG("Hijacked TCP connect(%d, peer_addr=%s)=%d, err=%s\n",
        this->fd,
        addr_str.c_str(),
        ret,
        ret == -1 ? strerror(errno) : "SUCCESS");

    return ret;
}

// int
// udp_fdesc::accept(struct sockaddr *addr, socklen_t *addrlen, fdesc_set &fdset)
// {
//     LOG("unexpected udp::accept(fd=%d)\n", this->fd);
//     debug_assert(0);
//     return -1;
// }

ssize_t
udp_fdesc::sendmsg(const struct msghdr * msg, int flags)
{
    PRELOAD_ORIG(sendmsg);
    ssize_t ret = sendmsg_orig(fd, msg, flags);
    LOG("Hijacked UDP sendmsg(%d, %p, %x)=%ld\n", this->fd, msg, flags, ret);
    return ret;
}

ssize_t
udp_fdesc::recv(void *buf, size_t len, int flags)
{
    PRELOAD_ORIG(recv);
    ssize_t ret = recv_orig(fd, buf, len, flags);
    LOG("Hijacked UDP recv(%d, %p, %ld, %x)=%ld\n", this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t udp_fdesc::recvfrom(void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    PRELOAD_ORIG(recvfrom);
    LOG("unexpected udp::recvfrom(fd=%d, buf=%p, len=%lu, flags=%x)\n",
        this->fd, buf, len, flags);
    debug_assert(0);
    return -1;
}

ssize_t udp_fdesc::recvmsg(struct msghdr *msg, int flags)
{
    PRELOAD_ORIG(recvmsg);
    ssize_t ret = recvmsg_orig(fd, msg, flags);
    LOG("Hijacked UDP recvmsg(%d, %p, %x)=%ld\n", this->fd, msg, flags, ret);
    return ret;
}

int udp_fdesc::bind(const struct sockaddr * addr, socklen_t addrlen)
{
    PRELOAD_ORIG(bind);
    this->self_port = ((struct sockaddr_in *)addr)->sin_port;

    int ret;
    struct sockaddr_un uds_addr = {.sun_family = AF_UNIX };
    sprintf(uds_addr.sun_path, "/ripc/emu-real-%d/listener:%hu", tls_selfid, ntohs(((struct sockaddr_in *)addr)->sin_port));
    LOG("uds_addr: %s\n", uds_addr.sun_path);
    ret = unlink(uds_addr.sun_path);
    LOG("unlink: %d\n", ret);
    if (ret < 0 && errno != ENOENT) {
        // fprintf(stderr, "bind_impl(): unlink [%s] before bind failed: %s", uds_addr.sun_path, strerror(errno));
        assert(0);
    }
    ret = bind_orig(this->fd, (const sockaddr *)&uds_addr, sizeof(uds_addr));
    std::string addr_str = paddr(addr, addrlen);
    LOG("Hijacked UDP bind(%d, %s)=%d\n",
        this->fd, addr_str.c_str(), ret);

    return ret;
}

bool udp_fdesc::poll_fastpath(struct pollfd *ufd)
{
    // TODO: check the log to mask out unordered events
    // TODO: check the log to return some message directly
    return false;
}

void udp_fdesc::poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd)
{
    ufd->revents = kfd->revents;
}

int
udp_fdesc::getsockopt_udp_socket_impl(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{

    switch (optname) {
    case SO_SNDBUF:
        *(int *)optval = this->sndbuf;
        *optlen = 4;
        break;
    case SO_RCVBUF:
        *(int *)optval = this->rcvbuf;
        *optlen = 4;
        break;
    case SO_ERROR:
        *(int *)optval = this->error;
        *optlen = 4;
        break;
    case SO_BINDTODEVICE:
        *(char *)optval = 0;
        *optlen = 0;
        break;
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::getsockopt_udp_ip_impl(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    switch (optname) {
    case IP_TOS:
        *(int *)optval = this->tos;
        *optlen = 4;
        break;
    case IP_TTL:
        *(int *)optval = this->ttl;
        *optlen = 4;
        break;
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::getsockopt_udp_udp_impl(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    switch (optname) {
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::getsockopt(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    int ret = 0;
    switch (level) {
    case SOL_SOCKET:
        ret = getsockopt_udp_socket_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_IP:
        ret = getsockopt_udp_ip_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_TCP:
        ret = getsockopt_udp_udp_impl(level, optname, optval, optlen);
        break;
    default:
        assert(0);
        return -1;
    }

    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Hijacked UDP getsockopt(%d, %s, %s, %u)=%d\n",
        this->fd, level_str.c_str(),
        optname_str.c_str(),
        *optlen, ret);
    return ret;
}


int
udp_fdesc::setsockopt_udp_socket_impl(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    switch (optname) {
    case SO_REUSEADDR:
        this->reuseaddr = *(int *)optval;
        break;
    case SO_REUSEPORT:
        this->reuseport = *(int *)optval;
        break;
    case SO_SNDBUF:
    case SO_SNDBUFFORCE:
        this->sndbuf = 2 * *(int *)optval;
        break;
    case SO_RCVBUF:
    case SO_RCVBUFFORCE:
        this->rcvbuf = 2 * *(int *)optval;
        break;
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::setsockopt_udp_ip_impl(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    switch (optname) {
    case IP_TOS:
        this->tos = *(int *)optval;
        break;
    case IP_TTL:
        this->ttl = *(int *)optval;
        break;
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::setsockopt_udp_udp_impl(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    switch (optname) {
    default:
        assert(0);
        break;
    }
    return 0;
}

int
udp_fdesc::setsockopt(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    LOG("at TCP setsockopt(), fd=%d\n", this->fd);
    int ret = 0;
    switch (level) {
    case SOL_SOCKET:
        ret = setsockopt_udp_socket_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_IP:
        ret = setsockopt_udp_ip_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_TCP:
        ret = setsockopt_udp_udp_impl(level, optname, optval, optlen);
        break;
    // TODO: IPPROTO_IPV6
    default:
        assert(0);
        return -1;
    }

    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Hijacked UDP setsockopt(%d, %s, %s, %u)=%d\n",
        this->fd, level_str.c_str(),
        optname_str.c_str(),
        optlen, ret);
    return ret;
}
