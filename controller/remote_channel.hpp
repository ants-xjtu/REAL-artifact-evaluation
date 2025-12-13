#pragma once

#include "const.hpp"
#include "message.hpp"
#include "ring_buffer.hpp"

#include <memory>
#include <mutex>
#include <deque>
#include <thread>
#include <unordered_map>
#include <condition_variable>

extern "C" {
#include <sys/epoll.h>
}

class RemoteChannel {
public:
    RemoteChannel(int fd, int host_id, int epoll_fd);
    std::vector<std::shared_ptr<Message>> pollin(int ctrl_rev_fd);
    void pollout();
    void add_msg(std::shared_ptr<Message> &msg) {
        std::unique_lock lock(mutex_);
        real_hdr_t *hdr = (real_hdr_t *)msg->data();
        real_hdr_t orig_hdr = *hdr;
        while ((size_t)orig_hdr.msg_len > rb_out_.capacity()) {
            rb_out_.expand();
        }
        send_queue_.push_back(msg);
        epoll_mod_add_out();
    }
    void send_eos(int stage);
    void send_keepbusy();
    int host_id() {
        return host_id_;
    }
private:
    std::deque<std::shared_ptr<Message>> send_queue_;
    int fd_;
    int host_id_;
    int epoll_fd_;
    uint32_t events_;
    std::mutex mutex_;
    RingBuffer rb_in_, rb_out_;
    inline void epoll_mod_add_out() {
        if (events_ & EPOLLOUT) {
            return;
        }
        events_ |= EPOLLOUT;
        struct epoll_event ev{ .events = events_, .data = {.fd = fd_} };
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd_, &ev);
    }
    inline void epoll_mod_del_out() {
        if (!(events_ & EPOLLOUT)) {
            return;
        }
        events_ &= ~EPOLLOUT;
        struct epoll_event ev{ .events = events_, .data = {.fd = fd_} };
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd_, &ev);
    }
};