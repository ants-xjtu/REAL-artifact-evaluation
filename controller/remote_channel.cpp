#include "remote_channel.hpp"
#include "const.hpp"
#include "replay_manager.hpp"

#include <iostream>

RemoteChannel::RemoteChannel(int fd, int host_id, int epoll_fd):
    fd_(fd), host_id_(host_id), epoll_fd_(epoll_fd), events_(EPOLLIN),
    rb_in_(1 << 20), rb_out_(1 << 20) {}

void RemoteChannel::send_eos(int stage) {
    real_hdr_t hdr{};
    hdr.msg_type = REAL_ENDOFSTAGE;
    hdr.msg_len = hdrsiz;
    hdr.seq = stage;
    auto msg = std::make_shared<Message>(sizeof(hdr));
    memcpy(msg->data(), &hdr, sizeof(hdr));
    msg->alloc_tail(sizeof(hdr));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        send_queue_.push_back(msg);
        epoll_mod_add_out();
    }
}

void RemoteChannel::send_keepbusy() {
    real_hdr_t hdr{};
    hdr.msg_type = REAL_KEEPBUSY;
    hdr.msg_len = hdrsiz;
    auto msg = std::make_shared<Message>(sizeof(hdr));
    memcpy(msg->data(), &hdr, sizeof(hdr));
    msg->alloc_tail(sizeof(hdr));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        send_queue_.push_back(msg);
        epoll_mod_add_out();
    }
}

void RemoteChannel::pollout() {
    std::unique_lock lock(mutex_);
    auto& rb = this->rb_out_;
    auto& queue = this->send_queue_;
    int fd = this->fd_;

    // Step1: move messages from queue to ringbuffer
    while (!queue.empty()) {
        auto& msg = queue.front();
        if ((size_t)msg->len() <= rb.availableWrite()) {
            bool ok = rb.put(msg->data(), msg->len());
            dbg_assert(ok, "rb.put(%p, %d) failed\n", msg->data(), msg->len());
            queue.pop_front();
        } else {
            break;
        }
    }

    // Step2: write to fd
    size_t to_send = rb.availableRead();
    if (to_send == 0) {
        if (queue.empty()) this->epoll_mod_del_out();
        return;
    }

    ssize_t n = rb.writeToFd(fd);
    if (n > 0) rb.consume((size_t)n);

    if (!queue.empty() || rb.availableRead()) {
        this->epoll_mod_add_out();
    } else {
        this->epoll_mod_del_out();
    }
}

std::vector<std::shared_ptr<Message>> RemoteChannel::pollin(int ctrl_rev_fd) {
    std::unique_lock lock(this->mutex_);
    // 1. read from fd into ringbuffer
    ssize_t n_read = rb_in_.readFromFd(fd_);
    if (n_read <= 0) {
        if (n_read == 0) {
            std::cout << "Host " << host_id_ << " closed connection\n";
            // TODO: cleanup
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("remote channel read()");
        }
        return {};
    }
    // 2. get messages from ringbuffer
    std::vector<std::shared_ptr<Message>> msg_list;
    while (rb_in_.availableRead() > 0) {
        real_hdr_t hdr;
        bool ok = rb_in_.peek(&hdr, sizeof(hdr));
        if (!ok) break;
        size_t msglen = hdr.msg_len; // type conversion
        while (msglen > rb_in_.capacity()) {
            rb_in_.expand();
        }
        if (rb_in_.availableRead() < msglen) {
            break;
        }
        auto msg = std::make_shared<Message>(msglen);
        msg->alloc_tail(msglen);
        ok = rb_in_.get(msg->data(), msglen);
        dbg_assert(ok, "rb_in_.get(%p, %ld) failed\n", msg->data(), msglen);
        msg_list.push_back(msg);
    }
    return msg_list;
}