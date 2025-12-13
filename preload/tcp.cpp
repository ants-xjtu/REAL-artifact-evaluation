#include "tcp.h"
#include "preload.h"
#include <set>
#include <atomic>

std::atomic<size_t> nxt_seq = 1;

bool tcp_fdesc::is_bgp_conn() const {
    return is_bgp_ && !is_listener;
}

static void send_one_msg(int fd, int peer_id, const void *buf, size_t count)
{
    PRELOAD_ORIG(writev);
    real_pld_t pld = (real_pld_t) {
        .hdr = (real_hdr_t) {
            .msg_type = REAL_PAYLOAD,
            .msg_len = (int)count + pldhdrsiz
            // seq is filled by controller
        },
        .src_id = tls_selfid,
        .dst_id = peer_id
    };
    struct iovec iov_out[2];
    iov_out[0].iov_base = &pld;
    iov_out[0].iov_len = pldhdrsiz;
    iov_out[1].iov_base = (void *)buf;
    iov_out[1].iov_len = count;
    int r = writev_orig(fd, iov_out, 2);
    if (r == (ssize_t)(pldhdrsiz + count)) {
        return;
    }
    if (r < 0) {
        r = 0;
    }
    int len1 = std::max(pldhdrsiz - r, 0);
    int len2 = std::min(count + pldhdrsiz - r, count);
    if (len1) {
        WRITE_UNTIL(fd, (char *)(&pld) + pldhdrsiz - len1, len1);
    }
    if (len2) {
        WRITE_UNTIL(fd, (char *)buf + count - len2, len2);
    }
}

static int fill_until(BgpMessage &msg, const char *buf, int count, int goal)
{
    if (msg.len >= goal) {
        return 0;
    }
    int filled = std::min(count, goal - msg.len);
    if (msg.buf == nullptr) {
        msg.buf = (char *)malloc(goal);
        msg.cap = goal;

        memcpy(msg.buf, buf, filled);
        msg.len = filled;
    } else {
        if (msg.len + filled > msg.cap) {
            msg.cap = msg.len + filled;
            msg.buf = (char *)realloc(msg.buf, msg.cap);
        }
        memcpy(msg.buf + msg.len, buf, filled);
        msg.len += filled;
    }
    return filled;
}

ssize_t tcp_fdesc::write(const void *buf, size_t count)
{
    PRELOAD_ORIG(write);
    if (!this->is_bgp_) {
        return write_orig(this->fd, buf, count);
    }
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
    int eaten = 0;
    int filled = 0;
    while (eaten < count) {
        int remlen = count - eaten;
        char *rembuf = ((char *)buf + eaten);

        // 1. fill until bgp_hdr can be parsed
        filled = fill_until(msg, rembuf, remlen, 19);
        eaten += filled;
        rembuf += filled;
        remlen -= filled;
        if (msg.len < 19) {
            debug_assert(eaten == count);
            break;
        }

        int bgplen = ntohs(((short *)msg.buf)[8]);
        int bgptype = ((char *)msg.buf)[18];

        // 2. fill until bgp msg is complete
        filled = fill_until(msg, rembuf, remlen, bgplen);
        eaten += filled;
        rembuf += filled;
        remlen -= filled;

        if (msg.len == bgplen) {
            LOG("Send BGP Message bgplen %d, type %d\n", bgplen, bgptype);
            send_one_msg(this->fd, this->peer_id, msg.buf, msg.len);
            free(msg.buf);
            msg = {nullptr, 0, 0};
        }
    }
    return count;
}

ssize_t tcp_fdesc::send(const void *buf, size_t len, int flags)
{
    PRELOAD_ORIG(send);
    LOG("Entering normal send(%d, %p, %ld, %x)\n",
        fd, buf, len, flags);
    assert(0);
    return -1;
}

ssize_t tcp_fdesc::writev(const struct iovec *iov, int iovcnt)
{
    PRELOAD_ORIG(writev);
    PRELOAD_ORIG(write);
    if (!this->is_bgp_) {
        return writev_orig(this->fd, iov, iovcnt);
    }
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
    int ret = 0;
    for (int i = 0; i < iovcnt; ++i) {
        ret += iov[i].iov_len;
    }
    for (int i = 0; i < iovcnt; ++i) {
        int eaten = 0;
        int filled = 0;
        while (eaten < iov[i].iov_len) {
            int remlen = iov[i].iov_len - eaten;
            char *rembuf = ((char *)iov[i].iov_base + eaten);

            // 1. fill until bgp_hdr can be parsed
            filled = fill_until(msg, rembuf, remlen, 19);
            eaten += filled;
            rembuf += filled;
            remlen -= filled;
            if (msg.len < 19) {
                debug_assert(eaten == iov[i].iov_len);
                break;
            }

            int bgplen = ntohs(((short *)msg.buf)[8]);
            int bgptype = ((char *)msg.buf)[18];

            // 2. fill until bgp msg is complete
            filled = fill_until(msg, rembuf, remlen, bgplen);
            eaten += filled;
            rembuf += filled;
            remlen -= filled;

            if (msg.len == bgplen) {
                LOG("Send BGP Message bgplen %d, type %d\n", bgplen, bgptype);
                send_one_msg(this->fd, this->peer_id, msg.buf, msg.len);
                free(msg.buf);
                msg = {nullptr, 0, 0};
            }
        }
    }
    return ret;
}

ssize_t tcp_fdesc::read_internal(char *buf, int buflen)
{
    PRELOAD_ORIG(read);
    PRELOAD_ORIG(recv);
    ssize_t ret;
    LOG("tcp_fdesc::read_internal(buf=%p, buflen=%d)\n",buf, buflen);

    /* Check is there any data to read */
    ret = recv_orig(fd, NULL, 0, MSG_PEEK);
    if (ret == -1 && !rcv_pending) {
        errno = EAGAIN;
        return -1;
    }

    /* 1. read the header */
    if (!this->rcv_pending) {
        READ_UNTIL(fd, &this->rcv_hdr, pldhdrsiz);
        this->rcv_offset = 0;
        this->rcv_pending = true;
    }

    /**
     * 1.5 read the payload up to buflen.
     * Note that we must not read the entire message,
     * otherwise epoll_wait() won't return this fd.
     */
    int n_copy = std::min((int)(this->rcv_hdr.hdr.msg_len - pldhdrsiz - rcv_offset), (int)buflen);
    READ_UNTIL(fd, buf, n_copy);

    /* 2. read the payload*/
    rcv_offset += n_copy;

#ifndef IMAGE_CRPD
    assert(nxt_seq == rcv_hdr.hdr.seq);
#endif
    nxt_msghdr_seen = false;

    if (rcv_offset == this->rcv_hdr.hdr.msg_len - pldhdrsiz) {
        // complete message
        rcv_pending = false;
        rcv_offset = 0;
        nxt_seq++;
    }

    return n_copy;
}

ssize_t tcp_fdesc::read(void *buf, size_t count)
{
    PRELOAD_ORIG(read);
    PRELOAD_ORIG(recv);
    if (!this->is_bgp_) {
        return read_orig(this->fd, buf, count);
    }
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }

    ssize_t ret;
    LOG("tcp_fdesc::read(buf=%p, count=%d)\n", buf, (int)count);

    if (this->pollhup) {
        LOG("pollhup\n");
        return 0;
    }

#ifndef IMAGE_CRPD
    if (!nxt_msghdr_seen) {
        // second read, not allowed by ppoll() yet
        errno = EAGAIN;
        return -1;
    }
#endif

    ret = read_internal((char *)buf, count);
    LOG("tcp_fdesc::read(buf=%p, count=%d) = %ld\n", buf, (int)count, ret);
    return ret;
}

ssize_t tcp_fdesc::readv(const struct iovec *iov, int iovcnt)
{
    PRELOAD_ORIG(read);
    PRELOAD_ORIG(readv);
    PRELOAD_ORIG(recv);
    if (!this->is_bgp_) {
        return readv_orig(this->fd, iov, iovcnt);
    }
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }

    ssize_t ret;
    LOG("tcp_fdesc::readv(iovcnt=%d)\n", iovcnt);

    if (this->pollhup) {
        return 0;
    }

#ifndef IMAGE_CRPD
    if (!nxt_msghdr_seen) {
        // second read, not allowed by ppoll() yet
        errno = EAGAIN;
        return -1;
    }
#endif

    ret = read_internal((char *)iov[0].iov_base, iov[0].iov_len);
    return ret;
}

int tcp_fdesc::getsockname(struct sockaddr *addr, socklen_t *addrlen)
{
    struct sockaddr_in *inaddr = (struct sockaddr_in *)addr;
    *inaddr = {
        .sin_family = AF_INET,
        .sin_port = self_port,
        .sin_addr = {self_addr}
    };
    *addrlen = sizeof(*inaddr);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked TCP getsockname(sockfd=%d, addr=%s)=0\n",
        fd, addr_str.c_str());
    return 0;
}

int tcp_fdesc::getpeername(struct sockaddr *addr, socklen_t *addrlen)
{
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
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

int tcp_fdesc::localhost_connect(const struct sockaddr *addr, socklen_t addrlen)
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
    struct sockaddr_in inet_srcaddr = {
        .sin_family = AF_INET,
        .sin_port = curr_port,
        .sin_addr = {tls_peers[peer_id].self_addr}
    };

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

    if (ret < 0) {
        if (errno == ENOENT) {
            errno = ECONNREFUSED;
        } else if (errno == EAGAIN) {
            errno = EINPROGRESS;
        }
    }

    this->peer_id = peer_id;
    this->peer_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
    this->peer_port =((struct sockaddr_in *)addr)->sin_port;
    this->self_addr = tls_peers[peer_id].self_addr;
    this->self_port = curr_port;

    addr_str = paddr_inet((struct sockaddr_in *)addr);
    LOG("Hijacked normal TCP connect(%d, peer_addr=%s)=%d, err=%s\n",
        this->fd,
        addr_str.c_str(),
        ret,
        ret == -1 ? strerror(errno) : "SUCCESS");

    if (ret == 0) {
        ret = -1;
        errno = EINPROGRESS;
        sock_state_ = REAL_TCP_ESTABLISHED;
    }

    return ret;
}

int tcp_fdesc::connect(const struct sockaddr *addr, socklen_t addrlen)
{
    PRELOAD_ORIG(connect);
    PRELOAD_ORIG(bind);
    const struct sockaddr_in *in_addr = ((const struct sockaddr_in *)addr);

    switch (this->sock_state_) {
    case REAL_TCP_CONN_REJECTED:
        assert(sk_err_ != 0);
        errno = sk_err_;
        return -1;
    case REAL_TCP_ESTABLISHED:
        errno = EISCONN;
        return -1;
    default:
        break;
    }

    this->is_bgp_ = ntohs(in_addr->sin_port) == BGP_PORT;
    if (!this->is_bgp_) {
        return localhost_connect(addr, addrlen);
    }

    // 0. prepare peer id
    int peer_id;
    struct sockaddr_in localhost;
    inet_aton("127.0.0.1", &localhost.sin_addr);
    if (in_addr->sin_addr.s_addr == localhost.sin_addr.s_addr) {
        peer_id = tls_selfid;
    } else {
        peer_id = tls_pip2peerid[in_addr->sin_addr.s_addr];
    }

    glb_fdset.set_nht_ready(peer_id);

    // 0.5 build mng channel
    struct sockaddr_un uds_srcaddr = {.sun_family = AF_UNIX };
    sprintf(uds_srcaddr.sun_path, "/ripc/emu-real-%d/%d", tls_selfid, peer_id);
    int ret = unlink(uds_srcaddr.sun_path);
    if (ret < 0 && errno != ENOENT) {
        fprintf(stderr, "connect_impl(): unlink [%s] before bind failed: %s", uds_srcaddr.sun_path, strerror(errno));
        assert(0);
    }
    ret = bind_orig(this->fd, (struct sockaddr *)&uds_srcaddr, sizeof(uds_srcaddr));
    if (ret < 0) {
        LOG("connect_impl(): bind(%d, addr=%s) for binding client to path failed: %s\n",
            this->fd, uds_srcaddr.sun_path, strerror(errno));
    }

    struct sockaddr_un mng_addr;
    mng_addr.sun_family = AF_UNIX;
    strncpy(mng_addr.sun_path, MNG_SOCKET_PATH, sizeof(mng_addr.sun_path) - 1);
    int r = connect_orig(this->fd, (const sockaddr*)&mng_addr, sizeof(mng_addr));
    if (r != 0) {
        fprintf(stderr, "connect_orig(this->fd=%d)=%d, error: %s\n", this->fd, r, strerror(errno));
    }
    debug_assert(r == 0);

    /* 1. construct SYN pakcet */
    real_syn_t syn = (real_syn_t) {
        .hdr = (real_hdr_t) {
            .msg_type = REAL_SYN,
            .msg_len = synsiz
        },
        .cli_id = tls_selfid,
        .svr_id = peer_id
    };

    /* 2. send SYN packet */
    WRITE_UNTIL(this->fd, &syn, synsiz);

    /* 3. wait for SYNACK */
    real_synack_t synack;
    READ_UNTIL(this->fd, &synack, synacksiz);
    assert(synack.hdr.msg_type == REAL_SYNACK);
    assert(synack.hdr.msg_len == sizeof(real_synack_t));

    if (synack.cli_port == 0) {
        // controller rejects the connection
        // the application should see:
        // - connect() returns -1, errno = EINPRGRESS
        // - later ppoll returns POLLOUT | POLLERR
        // - later getsockopt(SO_ERROR) returns ECONNREFUSED
        // TODO: support blocked connect()
        errno = EINPROGRESS;
        sk_err_ = ECONNREFUSED;
        sock_state_ = REAL_TCP_CONN_REJECTED;
        LOG("Hijacked BGP TCP connect(%d) rejected\n", this->fd);
        return -1;
    }

    this->peer_id = peer_id;
    this->peer_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
    this->peer_port =((struct sockaddr_in *)addr)->sin_port; // 179
    this->self_addr = tls_peers[peer_id].self_addr;
    this->self_port = htons(synack.cli_port);

    std::string addr_str = paddr_inet((struct sockaddr_in *)addr);
    LOG("Hijacked BGP TCP connect(%d, peer_addr=%s)\n",
        this->fd, addr_str.c_str());

    errno = EINPROGRESS;
    sock_state_ = REAL_TCP_ESTABLISHED;
    return -1;
}

int
tcp_fdesc::localhost_accept(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset)
{
    LOG("tcp::accept(fd=%d)\n", this->fd);
    PRELOAD_ORIG(accept)
    struct sockaddr_un uds_addr;
    socklen_t uds_socklen = sizeof(uds_addr);
    int ret = accept_orig(this->fd, (struct sockaddr *)&uds_addr, &uds_socklen);
    LOG("accept_orig(fd=%d)=%d\n", this->fd, ret);
    if (ret <= 0) {
        LOG("Hijacked TCP accept(fd=%d)=%d, error=%s\n",
            this->fd, ret, strerror(errno));
        return ret;
    }

    char peer_ipaddr[32];
    in_port_t port;
    int peerid;
    sscanf(uds_addr.sun_path, "/ripc/emu-real-%d/%[^:]:%hu", &peerid, peer_ipaddr, &port);
    port = htons(port);
    // LOG("uds_addr.sun_path: [%s]\n", uds_addr.sun_path);
    // LOG("peer_ipaddr: [%s], port: [%hu]\n", peer_ipaddr, port);
    // fill in addr and addrlen
    if (addr != nullptr) {
        ((struct sockaddr_in *)addr)->sin_family = AF_INET;
        inet_pton(AF_INET, peer_ipaddr, &((struct sockaddr_in *)addr)->sin_addr);
        ((struct sockaddr_in *)addr)->sin_port = port;
    }
    if (addrlen != nullptr) {
        *addrlen = sizeof(struct sockaddr_in);
    }
    auto fdesc_ptr = std::make_unique<tcp_fdesc>(
        ret,
        ((struct sockaddr_in *)addr)->sin_addr.s_addr,
        tls_peers[peerid].self_addr,
        port,
        this->self_port,
        peerid,
        false
    );
    fdesc_ptr->sock_state_ = REAL_TCP_ESTABLISHED;
    int ufd = fdset.emplace(std::move(fdesc_ptr));

    struct sockaddr_in self_addr;
    socklen_t self_addrlen = sizeof(struct sockaddr_in);
    ::getsockname(ufd, (struct sockaddr *)&self_addr, &self_addrlen);
    std::string selfaddr_str = paddr((struct sockaddr *)&self_addr, self_addrlen);
    std::string peeraddr_str = paddr(addr, *addrlen);
    LOG("Hijacked TCP accept(fd=%d, self_addr=%s,"
        "peer_addr=%s)=%d\n",
        this->fd,
        selfaddr_str.c_str(),
        peeraddr_str.c_str(),
        ufd);
    return ufd;
}

int
tcp_fdesc::accept4(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset)
{
    if (!this->is_bgp_) {
        return localhost_accept(addr, addrlen, flags, fdset);
    }
    LOG("tcp::accept4(fd=%d)\n", this->fd);
    PRELOAD_ORIG(accept4)
    int ret = accept4_orig(this->fd, nullptr, nullptr, flags);
    LOG("accept4_orig(fd=%d)=%d\n", this->fd, ret);
    if (ret <= 0) {
        LOG("Hijacked TCP accept(fd=%d)=%d, error=%s\n",
            this->fd, ret, strerror(errno));
        return ret;
    }

    /* receive the syn packet to know peer id */
    real_syn_t syn;
    READ_UNTIL(ret, &syn, synsiz);
    int peer_id = syn.cli_id;
    uint16_t port_no = htons(syn.cli_port); // network order

    /* fill the peer address */
    if (addr != nullptr) {
        ((struct sockaddr_in *)addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)addr)->sin_addr.s_addr = tls_peers[peer_id].peer_addr;
        ((struct sockaddr_in *)addr)->sin_port = port_no;
    }
    if (addrlen != nullptr) {
        *addrlen = sizeof(struct sockaddr_in);
    }

    /* create new fdesc */
    auto fdesc_ptr = std::make_unique<tcp_fdesc>(
        ret,
        ((struct sockaddr_in *)addr)->sin_addr.s_addr,
        tls_peers[peer_id].self_addr,
        port_no,
        this->self_port,
        peer_id,
        ntohs(this->self_port) == BGP_PORT
    );
    fdesc_ptr->sock_state_ = REAL_TCP_ESTABLISHED;
    int ufd = fdset.emplace(std::move(fdesc_ptr));

    /* just logging */
    struct sockaddr_in self_addr;
    socklen_t self_addrlen = sizeof(struct sockaddr_in);
    ::getsockname(ufd, (struct sockaddr *)&self_addr, &self_addrlen);
    std::string selfaddr_str = paddr((struct sockaddr *)&self_addr, self_addrlen);
    std::string peeraddr_str = paddr(addr, *addrlen);
    LOG("Hijacked TCP accept(fd=%d, self_addr=%s,"
        "peer_addr=%s)=%d\n",
        this->fd,
        selfaddr_str.c_str(),
        peeraddr_str.c_str(),
        ufd);
    return ufd;
}

ssize_t
tcp_fdesc::sendmsg(const struct msghdr * msg, int flags)
{
    PRELOAD_ORIG(sendmsg);
    debug_assert(0);
    return -1;
}

ssize_t
tcp_fdesc::recv(void *buf, size_t len, int flags)
{
    PRELOAD_ORIG(recv);
    debug_assert(0);
    return -1;
}

ssize_t tcp_fdesc::recvfrom(void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    PRELOAD_ORIG(recvfrom);
    debug_assert(0);
    return -1;
}

ssize_t tcp_fdesc::recvmsg(struct msghdr *msg, int flags)
{
    PRELOAD_ORIG(recvmsg);
    debug_assert(0);
    return -1;
}

ssize_t tcp_fdesc::sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (this->sock_state_ != REAL_TCP_ESTABLISHED) {
        errno = ENOTCONN;
        return -1;
    }
    PRELOAD_ORIG(sendto);
    std::string addr_str = paddr(dest_addr, addrlen);
    LOG("Entering TCP sendto(%d, %p, %ld, %x, %s)\n",
        fd, buf, len, flags, addr_str.c_str());
    LOG("TCP sendto() not implemented\n");
    debug_assert(0);
    return -1;
}

int tcp_fdesc::bind(const struct sockaddr * addr, socklen_t addrlen)
{
    PRELOAD_ORIG(bind);
    if (((struct sockaddr_in *)addr)->sin_port == 0) {
        return 0;
    }
    int orig_errno = errno;
    this->self_port = ((struct sockaddr_in *)addr)->sin_port;
    this->is_bgp_ = ntohs(this->self_port) == BGP_PORT;
    // TODO: switch to inet socket and replay fd operations for normal sockets

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
    if (ret == 0) {
        errno = orig_errno;
    }
    std::string addr_str = paddr(addr, addrlen);
    LOG("Hijacked TCP bind(%d, %s)=%d: %s\n",
        this->fd, addr_str.c_str(), ret, strerror(errno));

    return ret;
}

int tcp_fdesc::listen(int backlog)
{
    PRELOAD_ORIG(listen);
    this->is_listener = true;
    return listen_orig(this->fd, 1000);
}

bool tcp_fdesc::poll_fastpath(struct pollfd *ufd)
{
    // TODO: check the log to mask out unordered events
    // TODO: check the log to return some message directly

    // suppress accept() if some NHT is not ready
    if (is_listener && !glb_fdset.nht_all_ready()) {
        ufd->revents = 0;
        return true;
    }
    if (sock_state_ == REAL_TCP_CONN_REJECTED) {
        ufd->revents = ufd->events | POLLERR | POLLHUP;
        return true;
    }
    // suppress known unordered POLLIN()
    if (!is_listener && ufd->events == POLLIN && nxt_msghdr_seen && nxt_msghdr.hdr.seq != nxt_seq) {
        ufd->revents = 0;
        return true;
    }
    return false;
}

void tcp_fdesc::poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd)
{
    int revents = kfd->revents;
    if (is_listener || (revents & (POLLIN | POLLOUT)) != revents) {
        ufd->revents = revents;
        return;
    }
    if (revents & POLLIN) {
        if (!nxt_msghdr_seen) {
            PEEK_UNTIL(this->fd, &this->nxt_msghdr, pldhdrsiz);
            LOG("fd %d, revents %x, seq %ld, nxt_seq %ld\n", this->fd, revents, nxt_msghdr.hdr.seq, nxt_seq.load());
            nxt_msghdr_seen = true;
        }
        if (nxt_msghdr.hdr.seq != nxt_seq) {
            revents &= ~POLLIN;
        }
    }
    ufd->revents = revents;
}

int
tcp_fdesc::getsockopt_tcp_socket_impl(
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
        *(int *)optval = this->sk_err_;
        this->sk_err_ = 0;
        *optlen = 4;
        break;
    case SO_BINDTODEVICE:
        *(char *)optval = 0;
        *optlen = 0;
        break;
    default:
        fprintf(stderr, "optname=%d\n", optname);
        LOG("optname=%d\n", optname);
        assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::getsockopt_tcp_ip_impl(
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
        fprintf(stderr, "optname=%d\n", optname);
        assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::getsockopt_tcp_tcp_impl(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    switch (optname) {
    case TCP_INFO:
        // TODO: other info
        ((struct tcp_info *)optval)->tcpi_rtt = 100;
        *optlen = sizeof(struct tcp_info);
        break;
    case TCP_NODELAY:
        *(int *)optval = this->nodelay;
        *optlen = 4;
        break;
    case TCP_MAXSEG:
        *(int *)optval = this->maxseg;
        *optlen = 4;
        break;
    default:
        fprintf(stderr, "optname=%d\n", optname);
        assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::getsockopt(
    int level, int optname,
    void *optval, socklen_t *optlen
)
{
    int ret = 0;
    switch (level) {
    case SOL_SOCKET:
        ret = getsockopt_tcp_socket_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_IP:
        ret = getsockopt_tcp_ip_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_TCP:
        ret = getsockopt_tcp_tcp_impl(level, optname, optval, optlen);
        break;
    default:
        fprintf(stderr, "level=%d\n", level);
        assert(0);
        return -1;
    }

    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Hijacked TCP getsockopt(%d, %s, %s, %u)=%d\n",
        this->fd, level_str.c_str(),
        optname_str.c_str(),
        *optlen, ret);
    return ret;
}


int
tcp_fdesc::setsockopt_tcp_socket_impl(
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
    case SO_BINDTODEVICE:
    case SO_DONTROUTE:
        break;
    default:
        fprintf(stderr, "optname=%d\n", optname);
        // assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::setsockopt_tcp_ip_impl(
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
    case IP_MTU_DISCOVER:
        this->mtu_discover = *(int *)optval;
        break;
    default:
        fprintf(stderr, "optname=%d\n", optname);
        assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::setsockopt_tcp_tcp_impl(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    switch (optname) {
    case TCP_NODELAY:
        this->nodelay = *(int *)optval;
        break;
    case TCP_MAXSEG:
        this->maxseg = *(int *)optval;
        break;
    case TCP_QUICKACK:
        this->quickack = *(int *)optval;
        break;
    default:
        fprintf(stderr, "optname=%d\n", optname);
        assert(0);
        break;
    }
    return 0;
}

int
tcp_fdesc::setsockopt(
    int level, int optname,
    const void *optval, socklen_t optlen
)
{
    LOG("at TCP setsockopt(), fd=%d\n", this->fd);
    int ret = 0;
    switch (level) {
    case SOL_SOCKET:
        ret = setsockopt_tcp_socket_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_IP:
        ret = setsockopt_tcp_ip_impl(level, optname, optval, optlen);
        break;
    case IPPROTO_TCP:
        ret = setsockopt_tcp_tcp_impl(level, optname, optval, optlen);
        break;
    // TODO: IPPROTO_IPV6
    default:
        fprintf(stderr, "level=%d\n", level);
        assert(0);
        return -1;
    }

    std::string level_str = psockopt_level(level);
    std::string optname_str = psockopt_optname(level, optname, optval);
    LOG("Hijacked TCP setsockopt(%d, %s, %s, %u)=%d\n",
        this->fd, level_str.c_str(),
        optname_str.c_str(),
        optlen, ret);
    return ret;
}
