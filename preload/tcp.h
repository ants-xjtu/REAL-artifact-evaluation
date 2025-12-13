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

class tcp_fdesc;

struct BgpMessage {
    char *buf;
    int cap;
    int len;
};

enum TcpSockState {
    REAL_TCP_CLOSED,
    REAL_TCP_CONN_REJECTED,
    REAL_TCP_ESTABLISHED
};

class tcp_fdesc : public fdesc {
public:
    tcp_fdesc(
        int _fd,
        in_addr_t _peer_addr = 0,
        in_addr_t _self_addr = 0,
        in_port_t _peer_port = 0,
        in_port_t _self_port = 0,
        int _peer_id = -1,
        bool _is_bgp = false
    ) :
        fdesc(_fd, FDESC_TCP), pollhup(0), peer_id(_peer_id),
        peer_addr(_peer_addr), self_addr(_self_addr), peer_port(_peer_port), self_port(_self_port),
        reuseaddr(true), reuseport(true), sndbuf(65536), rcvbuf(65535),
        tos(192), ttl(255), mtu_discover(0),
        nodelay(0), maxseg(1500), fcntl_fd_flags(0),fcntl_st_flags(0),
        is_listener(false), is_bgp_(_is_bgp), sock_state_(REAL_TCP_CLOSED),
        msg({nullptr, 0, 0}), nxt_msghdr_seen(false),
        sk_err_(0), rcv_pending(false), rcv_offset(0)
    {
        LOG("tcp_fdesc(fd=%d)\n", this->fd);
    }
    ~tcp_fdesc() override
    {
        LOG("tcp_fdesc %d deconstruction\n", this->fd);
    }
    ssize_t write(const void *buf, size_t count) override;
    ssize_t writev(const struct iovec *iov, int iovcnt) override;
    ssize_t read(void *buf, size_t count) override;
    ssize_t readv(const struct iovec *iov, int iovcnt) override;
    int getsockname(struct sockaddr *addr, socklen_t *addrlen) override;
    int getpeername(struct sockaddr *addr, socklen_t *addrlen) override;
    int connect(const struct sockaddr *addr, socklen_t addrlen) override;
    int accept4(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset) override;
    ssize_t send(const void *buf, size_t len, int flags) override;
    ssize_t sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) override;
    ssize_t sendmsg(const struct msghdr * msg, int flags) override;
    ssize_t recv(void *buf, size_t len, int flags) override;
    ssize_t recvfrom(void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) override;
    ssize_t recvmsg(struct msghdr *msg, int flags) override;
    int bind(const struct sockaddr * addr, socklen_t addrlen) override;
    int listen(int backlog) override;
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
    friend int tcp_fcntl_impl(int ufd, int cmd, va_list args);
    bool is_bgp_conn() const override;

protected:
    bool pollhup;
    bool is_listener;
    bool is_bgp_;
    TcpSockState sock_state_;
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
    int sk_err_;
    /* IP options */
    int tos;
    int ttl;
    int mtu_discover;
    /* TCP options */
    bool nodelay;
    int maxseg;
    int quickack;
    int fcntl_fd_flags;
    int fcntl_st_flags;
    /* ongoing send message */
    BgpMessage msg;
    int bgp_len;
    /* next message to read */
    real_pld_t nxt_msghdr;
    bool nxt_msghdr_seen;
    /* read buffer */
    real_pld_t rcv_hdr;
    bool rcv_pending;
    ssize_t rcv_offset;

    int
    getsockopt_tcp_socket_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    getsockopt_tcp_ip_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    getsockopt_tcp_tcp_impl(
        int level, int optname,
        void *optval, socklen_t *optlen
    );
    int
    setsockopt_tcp_socket_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
    int
    setsockopt_tcp_ip_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
    int
    setsockopt_tcp_tcp_impl(
        int level, int optname,
        const void *optval, socklen_t optlen
    );
private:
    int localhost_connect(const struct sockaddr *addr, socklen_t addrlen);
    int localhost_accept(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset);
    ssize_t read_internal(char *buf, int buflen);
};
