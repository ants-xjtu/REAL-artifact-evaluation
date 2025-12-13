#pragma once

#include <array>
#include <unordered_set>
#include <bitset>
#include <memory>
#include <shared_mutex>
#include "debug.h"

extern "C" {

#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>

}

const int MAX_NFDS = 8192;

class fdesc_set;

enum fdesc_type_t {
    FDESC_NORMAL,
    FDESC_NETLINK,
    FDESC_TCP,
};

class fdesc {
public:
    fdesc(int _fd, fdesc_type_t _fdesc_type=FDESC_NORMAL) : fd(_fd), fdesc_type(_fdesc_type)
    {
        struct timespec _ts;
        clock_gettime(CLOCK_MONOTONIC, &_ts);
        LOG("fdesc(fd=%d)\n", this->fd);
    }
    virtual ~fdesc() {};
    virtual ssize_t write(const void *buf, size_t count);
    virtual ssize_t writev(const struct iovec *iov, int iovcnt);
    virtual ssize_t read(void *buf, size_t count);
    virtual ssize_t readv(const struct iovec *iov, int iovcnt);
    // int ioctl(unsigned long request, ...);
    virtual int getsockname(struct sockaddr *addr, socklen_t *addrlen);
    virtual int getpeername(struct sockaddr *addr, socklen_t *addrlen);
    virtual int connect(const struct sockaddr *addr, socklen_t addrlen);
    virtual int accept4(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset);
    virtual ssize_t send(const void *buf, size_t len, int flags);
    virtual ssize_t sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
    virtual ssize_t sendmsg(const struct msghdr * msg, int flags);
    virtual ssize_t recv(void *buf, size_t len, int flags);
    virtual ssize_t recvfrom(
        void *buf, size_t len, int flags,
        struct sockaddr *src_addr, socklen_t *addrlen);
    virtual ssize_t recvmsg(struct msghdr *msg, int flags);
    virtual int close();
    virtual int bind(const struct sockaddr * addr, socklen_t addrlen);
    virtual int listen(int backlog);
    virtual int getsockopt(int level, int optname, void *optval, socklen_t *optlen);
    virtual int setsockopt(int level, int optname, const void *optval, socklen_t optlen);
    virtual bool is_bgp_conn() const {
        return false;
    };

    // Fill ufd with any known ready events of the fd.
    // Returns true indicates the poll succeeded, causing
    // slow path to be skipped for this fd.
    virtual bool poll_fastpath(struct pollfd *ufd);
    virtual void poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd);
    fdesc_type_t type() const { return fdesc_type; }

    friend class fdesc_set;

protected:
    int fd;
    fdesc_type_t fdesc_type;
private:
    // TODO: maintain wait_events for epoll
};

class fdesc_set {
public:
    // whether fd is recorded (as ufd)
    bool contains(int fd);

    int close(int fd);
    int remove(int fd);

    // returns user-space fd
    int emplace(std::unique_ptr<fdesc> &&fdesc_ptr);

    // returns unique_ptr to the fdesc (tread fd as ufd)
    std::unique_ptr<fdesc> &at(int fd);

    int poll_fastpath(
        struct pollfd *ufds,
        struct pollfd *kfds,
        const nfds_t nfds
    );

    int poll_slowpath(
        struct pollfd *ufds,
        const struct pollfd *kfds,
        const nfds_t nfds
    );

    void set_nht_ready(int peerid) {
        std::shared_lock lock(mutex_);
        nht_ready_.insert(peerid);
    }

    bool nht_all_ready();

private:
    int null_fd = 0;
    std::array<std::unique_ptr<fdesc>, MAX_NFDS> fd2ptr_;
    std::bitset<MAX_NFDS> managed_;
    std::shared_mutex mutex_;
    std::unordered_set<int> nht_ready_;

    bool contains_internal(int fd);
    int emplace_internal(std::unique_ptr<fdesc> &&fdesc_ptr);
};
