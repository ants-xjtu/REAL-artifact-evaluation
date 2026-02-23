#include "channel.hpp"
#include "const.hpp"
#include <algorithm>
#include <mutex>

extern "C" {
#include <time.h>
}

extern long gettime_ns(int clock_id);

class ChannelManager {
public:
    void init(int n_nodes) {
        for (int u = 0; u <= n_nodes; ++u) {
            id2fd.emplace_back(n_nodes + 1);
        }
        baseline_ts_ = gettime_ns(CLOCK_MONOTONIC);
    }
    std::shared_ptr<Channel> get(int node_id, int peer_id) {
        if (id2fd[node_id][peer_id] <= 0 || id2fd[node_id][peer_id] >= MAX_CONNS) {
            return nullptr;
        }
        return fd2ch[id2fd[node_id][peer_id]];
    }
    std::shared_ptr<Channel> get_by_fd(int fd) {
        return fd2ch[fd];
    }
    template <class... Args>
    std::shared_ptr<Channel> make_channel(int fd, int epfd, int self_id, int peer_id, uint32_t events, Channel::ChannelState init_state) {
        id2fd[self_id][peer_id] = fd;
        record_channel_creation(self_id, peer_id);
        return fd2ch[fd] = std::make_shared<Channel>(fd, epfd, self_id, peer_id, events, init_state);
    }
    void delete_channel(int fd) {
        auto ch = fd2ch[fd];
        if (ch) {
            id2fd[ch->self_id()][ch->peer_id()] = 0;
        }
        fd2ch[fd] = nullptr;
    }

    // Latency tracking for bidirectional channel pairs
    void record_channel_creation(int self_id, int peer_id) {
        // Use ordered pair as key to identify bidirectional pair
        auto key = std::make_pair(std::min(self_id, peer_id), std::max(self_id, peer_id));
        long ts = gettime_ns(CLOCK_MONOTONIC);
        std::lock_guard<std::mutex> lock(latency_mutex_);
        completion_ts_[key] = ts;
    }

    void export_latency_csv();

private:
    std::vector<std::vector<int>> id2fd;
    std::array<std::shared_ptr<Channel>, MAX_CONNS> fd2ch;

    // Latency tracking for bidirectional channel pairs
    long baseline_ts_ = 0;
    std::mutex latency_mutex_;
    std::unordered_map<std::pair<int,int>, long, pair_hash> completion_ts_;      // completion timestamps for pairs
};

extern ChannelManager g_channel_manager;
