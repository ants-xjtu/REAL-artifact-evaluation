#pragma once

#include "debug.hpp"
#include "ring_buffer.hpp"
#include "message.hpp"

#include <memory>
#include <unordered_map>
#include <queue>
#include <cstdint>
#include <cstring>
#include <mutex>

extern "C" {
#include <sys/epoll.h>
}

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ h2;
    }
};

class Channel {
public:
    enum ChannelState {
        CONN_INPROGRESS,
        ACCEPTED,
        CHANNEL_ESTABLISHED,
        BGP_ESTABLISHED
    };
    Channel(int fd, int epfd, int self_id, int peer_id, uint32_t events, ChannelState init_state);
    ~Channel();
    uint16_t alloc_port();
    void sendmsg(std::shared_ptr<Message> &msg);
    std::vector<std::shared_ptr<Message>> pollin();
    void pollout();
    bool pollerr(int event); // destroy the connection or not
    int self_id() {
        return self_id_;
    }
    int peer_id() {
        return peer_id_;
    }
    bool bgp_is_established() {
        return established_;
    }
    ChannelState state() {
        return state_;
    }
    void on_connect_ok();
    void on_receive_syn();
    void on_bgp_established();
    static std::atomic<int> n_channel;

private:
    ChannelState state_;
    static std::mutex port_mng_mutex;
    static std::unordered_map<int, uint16_t> next_port;
    static std::unordered_map<std::pair<int,int>, uint16_t, pair_hash> port_store;

    int fd_; // to input/output through syscall
    int epfd_;
    int self_id_;
    int peer_id_;
    uint32_t events_;
    std::queue<std::shared_ptr<Message>> pending_out_msgs_; // owns messages
    bool established_;
    RingBuffer rb_in_, rb_out_;
};
