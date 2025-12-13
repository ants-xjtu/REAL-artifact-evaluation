#pragma once

#include "const.hpp"
#include "message.hpp"

#include <vector>
#include <queue>
#include <memory>
#include <cstring>
#include <iostream>
#include <atomic>
#include <mutex>
#include <unordered_set>

class ReplayManager {
public:
    struct history_msg {
        int src_id;
        long timestamp;
        std::shared_ptr<Message> msg;
    };
    void init(int max_node_id) {
        has_new_msg_ = false;
        delayed_msg_list_.resize(max_node_id + 1);
        msg_list_.resize(max_node_id + 1);
        replayed_seq_.resize(max_node_id + 1);
        restore_until_seq_.resize(max_node_id + 1);
    }
    void add_msg(std::shared_ptr<Message> &msg, int src_id, int dst_id);
    bool has_new_msg() {
        return has_new_msg_;
    }
    void node_offline(int node_id) {
        std::unique_lock lock(node_mutex_[node_id]);
        restore_until_seq_[node_id] = msg_list_[node_id].size();
        replayed_seq_[node_id] = 0;
    }
    // TODO: maybe we should wait for reactions after a replay,
    // otherwise the app may be not expecting the message yet.
    bool node_replay_one_msg(int node_id);
    void new_iteration()
    {
        has_new_msg_ = false;
    }
    void export_iolog();
private:
    /* per dst_node */
    std::vector<std::vector<history_msg>> delayed_msg_list_;
    std::vector<std::vector<history_msg>> msg_list_;
    std::vector<size_t> replayed_seq_;
    std::vector<size_t> restore_until_seq_;
    // std::vector<std::mutex> doesn't compile: std::mutex cannot be moved/copied around
    std::array<std::mutex, MAX_CLIENTS> node_mutex_;
    bool has_new_msg_;
    void try_flush_delayed_msg(int dst_id);
};

extern ReplayManager g_replay_mnger;
