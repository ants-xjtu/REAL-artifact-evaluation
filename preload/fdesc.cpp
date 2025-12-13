#include "fdesc.h"
#include "preload.h"
#include "debug.h"
#include "util.h"

#include <mutex>

bool fdesc_set::nht_all_ready() {
    std::shared_lock lock(mutex_);
    return nht_ready_.size() == tls_peers.size();
}

bool fdesc_set::contains_internal(int fd) {
    std::shared_lock lock(mutex_);
    return 0 <= fd && fd < MAX_NFDS && managed_.test(fd) && fd2ptr_.at(fd) != nullptr;
}

int fdesc_set::close(int fd)
{
    PRELOAD_ORIG(open);
    PRELOAD_ORIG(close);
    PRELOAD_ORIG(fcntl);
    int new_fd = syscall(SYS_dup, fd);
    if (null_fd == 0) {
        null_fd = open_orig("/dev/null", O_RDONLY | O_CLOEXEC);
        assert(null_fd > 0);
    }
    syscall(SYS_dup2, null_fd, fd);
    int flags = fcntl_orig(fd, F_GETFD);
    fcntl_orig(fd, F_SETFD, flags | FD_CLOEXEC);
    fd2ptr_[fd] = nullptr;
    return close_orig(new_fd);
}

int fdesc_set::remove(int fd)
{
    if (fd >= 0 && fd < MAX_NFDS) {
        std::unique_lock lock(mutex_);
        fd2ptr_[fd] = nullptr;
        managed_.reset(fd);
    }
    return 0;
}

int fdesc_set::emplace_internal(std::unique_ptr<fdesc> &&fdesc_ptr) {
    int fd = fdesc_ptr->fd;
    assert(!managed_.test(fd));
    managed_.set(fd);
    fd2ptr_[fd] = std::move(fdesc_ptr);
    return fd;
}

bool fdesc_set::contains(int fd)
{
    std::shared_lock lock(mutex_);
    return contains_internal(fd);
}

int fdesc_set::emplace(std::unique_ptr<fdesc> &&fdesc_ptr) {
    std::unique_lock lock(mutex_);
    return emplace_internal(std::move(fdesc_ptr));
}

std::unique_ptr<fdesc> &fdesc_set::at(int fd) {
    std::shared_lock lock(mutex_);
    return fd2ptr_.at(fd);
}

int fdesc_set::poll_fastpath(
    struct pollfd *ufds,
    struct pollfd *kfds,
    const nfds_t nfds
)
{
    int r = 0;
    std::shared_lock lock(mutex_);
    for (nfds_t i = 0; i < nfds; ++i) {
        if (ufds[i].fd < 0) {
            kfds[i] = ufds[i];
            continue;
        }
        ufds[i].revents = 0;
        kfds[i] = ufds[i];

        int fd = ufds[i].fd;
        // panic on out of range fd
        if (fd >= MAX_NFDS) {
            fprintf(stderr, "[WARN] fd %d larger than MAX_NFDS(%d)\n", fd, MAX_NFDS);
            for (int i = 0; i < nfds; ++i) {
                LOG("  ufds[%d]: fd=%d, events=%x\n", i, ufds[i].fd, ufds[i].events);
            }
            log_backtrace();
            assert(0);
            continue;
        }
        if (!managed_.test(fd)) {
            // just pass to kernel
            // TODO: is this a problem?
            continue;
        }
        if (fd2ptr_[fd] == nullptr) {
            LOG("fd2ptr_.at(%d) == nullptr\n", fd);
            kfds[i].events = 0;
            ufds[i].revents = POLLNVAL;
            r++;
            continue;
        }

        // assumes valid fd below.
        kfds[i].fd = fd;
        if (fd2ptr_[fd]->poll_fastpath(ufds + i)) {
            // don't pass into kernel
            kfds[i].events = 0;
        }
        // Some fast path may indicate that we should
        // return 0 for revents and bypass kernel's poll()
        if (ufds[i].revents) {
            r++;
        }
    }
    return r;
}

int fdesc_set::poll_slowpath(
    struct pollfd *ufds,
    const struct pollfd *kfds,
    const nfds_t nfds
)
{
    int r = 0;
    std::shared_lock lock(mutex_);
    for (nfds_t i = 0; i < nfds; ++i) {
        if (ufds[i].fd < 0) {
            continue;
        }
        int fd = ufds[i].fd;
        if (kfds[i].events != 0) {
            // fd is passed to kernel, propagate kfds to ufds
            if (managed_.test(fd) && fd2ptr_[fd] != nullptr) {
                fd2ptr_[fd]->poll_slowpath(ufds + i, kfds + i);
            } else {
                ufds[i] = kfds[i];
            }
        }
        if (ufds[i].revents) {
            r++;
        }
    }
    return r;
}

int fdesc::listen(int backlog)
{
    PRELOAD_ORIG(listen);
    return listen_orig(fd, backlog);
}

ssize_t fdesc::write(const void *buf, size_t count)
{
    PRELOAD_ORIG(write);
    return write_orig(fd, buf, count);
}

ssize_t fdesc::writev(const struct iovec *iov, int iovcnt)
{
    PRELOAD_ORIG(writev);
    return writev_orig(fd, iov, iovcnt);
}

ssize_t fdesc::readv(const struct iovec *iov, int iovcnt)
{
    PRELOAD_ORIG(readv);
    return readv_orig(fd, iov, iovcnt);
}

ssize_t fdesc::read(void *buf, size_t count)
{
    PRELOAD_ORIG(read);
    return read_orig(fd, buf, count);
}

int fdesc::getsockname(struct sockaddr *addr, socklen_t *addrlen)
{
    PRELOAD_ORIG(getsockname);
    int ret = getsockname_orig(fd, addr, addrlen);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked normal getsockname(sockfd=%d, addr=%s)=%d\n",
        this->fd, addr_str.c_str(), ret);
    return ret;
}

int fdesc::getpeername(struct sockaddr *addr, socklen_t *addrlen)
{
    PRELOAD_ORIG(getpeername);
    int ret = getpeername_orig(fd, addr, addrlen);
    std::string addr_str = paddr(addr, *addrlen);
    LOG("Hijacked normal getpeername(sockfd=%d, addr=%s)=%d\n",
        this->fd, addr_str.c_str(), ret);
    return ret;
}

int fdesc::connect(const struct sockaddr *addr, socklen_t addrlen)
{
    PRELOAD_ORIG(connect);

    int ret = connect_orig(fd, addr, addrlen);
    LOG("Hijacked normal connect(%d, peer_addr=%s)=%d, err=%s\n",
        this->fd,
        ((struct sockaddr_un *)addr)->sun_path,
        ret,
        ret == -1 ? strerror(errno) : "SUCCESS");
    return ret;
}

int fdesc::accept4(struct sockaddr *addr, socklen_t *addrlen, int flags, fdesc_set &fdset)
{
    LOG("Entering fdesc::accept()\n");
    PRELOAD_ORIG(accept4);
    int ret = accept4_orig(fd, addr, addrlen, flags);
    if (ret >= 0) {
        glb_fdset.emplace(std::make_unique<fdesc>(ret));
    }
    LOG("Entering fdesc::accept4(%d) = %d\n", fd, ret);
    return ret;
}

ssize_t fdesc::sendmsg(const struct msghdr *msg, int flags)
{
    PRELOAD_ORIG(sendmsg);
    ssize_t ret = sendmsg_orig(fd, msg, flags);
    LOG("Hijacked normal sendmsg(%d, %p, %x)=%ld\n", this->fd, msg, flags, ret);
    return ret;
}

ssize_t fdesc::recv(void *buf, size_t len, int flags)
{
    PRELOAD_ORIG(recv);
    LOG("Entering normal recv(%d, %p, %ld, %x)\n",
        fd, buf, len, flags);
    ssize_t ret = recv_orig(fd, buf, len, flags);
    LOG("Hijacked normal recv(%d, %p, %ld, %x)=%ld\n", this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t fdesc::recvfrom(void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    PRELOAD_ORIG(recvfrom);
    LOG("Entering normal recvfrom(%d, %p, %ld, %x)\n",
        fd, buf, len, flags);
    ssize_t ret = recvfrom_orig(fd, buf, len, flags, src_addr, addrlen);
    LOG("Hijacked normal recvfrom(sockfd=%d, buf=%p, len=%ld, flags=%x)=%ld\n",
            this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t fdesc::send(const void *buf, size_t len, int flags)
{
    PRELOAD_ORIG(send);
    LOG("Entering normal send(%d, %p, %ld, %x)\n",
        fd, buf, len, flags);
    ssize_t ret = send_orig(fd, buf, len, flags);
    LOG("Hijacked normal send(sockfd=%d, buf=%p, len=%ld, flags=%x)=%ld\n",
            this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t fdesc::sendto(const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    PRELOAD_ORIG(sendto);
    LOG("Entering normal sendto(%d, %p, %ld, %x)\n",
        fd, buf, len, flags);
    ssize_t ret = sendto_orig(fd, buf, len, flags, dest_addr, addrlen);
    LOG("Hijacked normal sendto(sockfd=%d, buf=%p, len=%ld, flags=%x)=%ld\n",
            this->fd, buf, len, flags, ret);
    return ret;
}

ssize_t fdesc::recvmsg(struct msghdr *msg, int flags)
{
    PRELOAD_ORIG(recvmsg);
    ssize_t ret = recvmsg_orig(fd, msg, flags);
    LOG("Hijacked normal recvmsg(%d, %p, %x)=%ld\n", this->fd, msg, flags, ret);
    return ret;
}

int fdesc::close()
{
    PRELOAD_ORIG(close);
    return close_orig(fd);
}

int fdesc::bind(const struct sockaddr *addr, socklen_t addrlen)
{
    PRELOAD_ORIG(bind);
    return bind_orig(fd, addr, addrlen);
}

bool fdesc::poll_fastpath(struct pollfd *ufd)
{
    return false;
}

void fdesc::poll_slowpath(struct pollfd *ufd, const struct pollfd *kfd)
{
    ufd->revents = kfd->revents;
}

int fdesc::getsockopt(int level, int optname, void *optval, socklen_t *optlen)
{
    PRELOAD_ORIG(getsockopt);
    return getsockopt_orig(fd, level, optname, optval, optlen);
}

int fdesc::setsockopt(int level, int optname, const void *optval, socklen_t optlen)
{
    PRELOAD_ORIG(setsockopt);
    LOG("at normal setsockopt(), fd=%d\n", this->fd);
    return setsockopt_orig(fd, level, optname, optval, optlen);
}
