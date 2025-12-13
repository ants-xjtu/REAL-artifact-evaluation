#pragma once

#include "debug.h"
#include "preload.h"
#include "netlink.h"
#include "util.h"
#include "fdesc.h"

#include <shared_mutex>
#include <thread>
#include <memory>
#include <set>

class udp_fdesc;

class udp_fdesc : public fdesc {
public:
    udp_fdesc(
        int _fd,
        in_addr_t _peer_addr = 0,
        in_addr_t _self_addr = 0,
        in_port_t _peer_port = 0,
        in_port_t _self_port = 0,
        int _peer_id = -1
    ) :
        fdesc(_fd, FDESC_TCP), pollhup(0), peer_id(_peer_id),
        peer_addr(_peer_addr), self_addr(_self_addr), peer_port(_peer_port), self_port(_self_port),
        reuseaddr(true), reuseport(true), sndbuf(65536), rcvbuf(65535),
        tos(192), ttl(255)
    {
        LOG("udp_fdesc(fd=%d)\n", this->fd);
    }
    ~udp_fdesc() override
    {
        LOG("udp_fdesc %d deconstruction\n", this->fd);
    }
    ssize_t write(const void *buf, size_t count) override;
    ssize_t writev(const struct iovec *iov, int iovcnt) override;
    ssize_t read(void *buf, size_t count) override;
    int getsockname(struct sockaddr *addr, socklen_t *addrlen) override;
    int getpeername(struct sockaddr *addr, socklen_t *addrlen) override;
    int connect(const struct sockaddr *addr, socklen_t addrlen) override;
    // int accept(struct sockaddr *addr, socklen_t *addrlen, fdesc_set &fdset) override;
    ssize_t sendmsg(const struct msghdr * msg, int flags) override;
    ssize_t recv(void *buf, size_t len, int flags) override;
    ssize_t recvfrom(void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) override;
    ssize_t recvmsg(struct msghdr *msg, int flags) override;
    int bind(const struct sockaddr * addr, socklen_t addrlen) override;
    bool poll_fastpath(struct pollfd *ufd) override;
    void poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd) override;
    int setsockopt(
        int level, int optname,
        const void *optval, socklen_t optlen
    ) override;
    int getsockopt(
        int level, int optname,
        void *optval, socklen_t *optlen
    ) override;
    friend int tcp_fcntl(int fd, int cmd, ...);
    bool bgp_sent_keepalive();
    bool bgp_recved_keepalive();

protected:
    bool pollhup;
    // int pollhup_revents;
    /* in network order */
    in_addr_t peer_addr;
    in_addr_t self_addr;
    in_port_t peer_port;
    in_port_t self_port;
    /* in network order end */
    int peer_id;
    /* socket options */
    bool reuseaddr;
    bool reuseport;
    int sndbuf;
    int rcvbuf;
    int error;
    /* IP options */
    int tos;
    int ttl;
    /* UDP options */
    /* options end */

    int
    getsockopt_udp_socket_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    getsockopt_udp_ip_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    getsockopt_udp_udp_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    setsockopt_udp_socket_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
    int
    setsockopt_udp_ip_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
    int
    setsockopt_udp_udp_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
};
