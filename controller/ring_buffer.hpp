#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/uio.h>

constexpr int RINGBUFFER_IN_SIZ = (1 << 12);
constexpr int RINGBUFFER_OUT_SIZ = (1 << 12);

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buf_(capacity ? capacity : 1), cap_(buf_.size()), r_(0), w_(0) {}

    size_t capacity() const noexcept { return cap_; }

    // Readable / writable
    size_t availableRead()  const noexcept { return static_cast<size_t>(w_ - r_); }
    size_t availableWrite() const noexcept { return cap_ - availableRead(); }

    // ---- Added: write from pointer ----
    // Write up to len bytes from a pointer. Returns the actual number of bytes
    // written (may be less than len).
    size_t write(const void* src, size_t len) {
        if (len == 0) return 0;
        if (src == nullptr) return 0;

        size_t freeb = availableWrite();
        if (freeb == 0) return 0;

        size_t to_write = (len < freeb) ? len : freeb;

        size_t wmod  = static_cast<size_t>(w_ % cap_);
        size_t tail  = cap_ - wmod;
        size_t first = (to_write < tail) ? to_write : tail;
        std::memcpy(&buf_[wmod], src, first);

        size_t remain = to_write - first;
        if (remain) {
            std::memcpy(&buf_[0], static_cast<const uint8_t*>(src) + first, remain);
        }
        w_ += to_write;
        return to_write;
    }

    // Must write len bytes atomically; otherwise do not write and return false
    bool put(const void* src, size_t len) {
        if (len == 0) return true;
        if (src == nullptr) return false;
        if (availableWrite() < len) return false;

        size_t wmod  = static_cast<size_t>(w_ % cap_);
        size_t tail  = cap_ - wmod;
        size_t first = (len < tail) ? len : tail;
        std::memcpy(&buf_[wmod], src, first);
        size_t remain = len - first;
        if (remain) {
            std::memcpy(&buf_[0], static_cast<const uint8_t*>(src) + first, remain);
        }
        w_ += len;
        return true;
    }

    // Read from fd (at most two segments)
    ssize_t readFromFd(int fd) {
        if (cap_ == 0) { errno = EINVAL; return -1; }
        size_t total = 0;

        auto [p1, n1] = freeWindow();
        if (n1 == 0) { errno = EAGAIN; return -1; }
        ssize_t r = retryRead(fd, p1, n1);
        if (r > 0) {
            advanceWrite(static_cast<size_t>(r));
            total += static_cast<size_t>(r);
        } else {
            return (total > 0) ? static_cast<ssize_t>(total) : r;
        }

        if (availableWrite() > 0) {
            auto [p2, n2] = freeWindow();
            if (n2 > 0) {
                r = retryRead(fd, p2, n2);
                if (r > 0) {
                    advanceWrite(static_cast<size_t>(r));
                    total += static_cast<size_t>(r);
                } else if (r == 0) {
                    return static_cast<ssize_t>(total);
                } else {
                    if (total > 0) return static_cast<ssize_t>(total);
                    return -1;
                }
            }
        }
        return static_cast<ssize_t>(total);
    }

    // Only copy (do not consume)
    bool peek(void* dst, size_t len) const {
        if (len > availableRead() || dst == nullptr) return false;
        size_t rmod  = static_cast<size_t>(r_ % cap_);
        size_t first = std::min(len, cap_ - rmod);
        std::memcpy(dst, &buf_[rmod], first);
        size_t remain = len - first;
        if (remain) std::memcpy(static_cast<uint8_t*>(dst) + first, &buf_[0], remain);
        return true;
    }

    // Consume
    bool consume(size_t len) {
        if (len > availableRead()) return false;
        r_ += len;
        return true;
    }

    // Copy and consume
    bool get(void* dst, size_t len) {
        if (!peek(dst, len)) return false;
        return consume(len);
    }

    // Optional: return a two-part iovec (does not consume)
    ssize_t writeToFd(int fd) const {
        size_t len = availableRead();
        struct iovec out[2];
        size_t rmod  = static_cast<size_t>(r_ % cap_);
        size_t first = std::min(len, cap_ - rmod);
        out[0].iov_base = const_cast<uint8_t*>(&buf_[rmod]);
        out[0].iov_len  = first;
        out[1].iov_base = const_cast<uint8_t*>(&buf_[0]);
        out[1].iov_len  = len - first;
        return writev(fd, out, 2);
    }

    void expand() {
        const size_t used = availableRead();
        size_t new_cap = cap_ * 2;
        std::vector<uint8_t> nb(new_cap);
        peek(nb.data(), used);
        buf_.swap(nb);
        cap_ = new_cap;
        r_   = 0;
        w_   = used;
    }

private:
    // Current contiguous free window (at the write pointer)
    std::pair<uint8_t*, size_t> freeWindow() {
        size_t freeb = availableWrite();
        if (freeb == 0) return {nullptr, 0};
        size_t wmod = static_cast<size_t>(w_ % cap_);
        size_t tail = cap_ - wmod;
        return { &buf_[wmod], std::min(freeb, tail) };
    }

    static ssize_t retryRead(int fd, uint8_t* p, size_t n) {
        ssize_t r;
        do { r = ::read(fd, p, n); } while (r == -1 && errno == EINTR);
        return r;
    }

    void advanceWrite(size_t n) { w_ += n; }

private:
    std::vector<uint8_t> buf_;
    size_t               cap_;
    uint64_t             r_;   // monotonically increasing
    uint64_t             w_;   // monotonically increasing
};
