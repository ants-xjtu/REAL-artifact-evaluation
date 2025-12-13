#include "channel.hpp"
#include "const.hpp"

class ChannelManager {
public:
    void init(int n_nodes) {
        for (int u = 0; u <= n_nodes; ++u) {
            id2fd.emplace_back(n_nodes + 1);
        }
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
        return fd2ch[fd] = std::make_shared<Channel>(fd, epfd, self_id, peer_id, events, init_state);
    }
    void delete_channel(int fd) {
        auto ch = fd2ch[fd];
        if (ch) {
            id2fd[ch->self_id()][ch->peer_id()] = 0;
        }
        fd2ch[fd] = nullptr;
    }
private:
    std::vector<std::vector<int>> id2fd;
    std::array<std::shared_ptr<Channel>, MAX_CONNS> fd2ch;
};

extern ChannelManager g_channel_manager;
