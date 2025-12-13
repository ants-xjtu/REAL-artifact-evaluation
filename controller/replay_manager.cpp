#include "replay_manager.hpp"
#include "channel.hpp"
#include "channel_manager.hpp"
#include "remote_channel.hpp"

#include <fstream>
#include <algorithm>
#include <bitset>

extern volatile std::atomic<int> stage;
extern int n_nodes;
extern std::string logPath;
extern int iteration_idx;
extern std::vector<std::unordered_set<int>> glb_local_parts;
extern std::unordered_set<int> glb_local_cut;
extern std::array<int, MAX_CLIENTS> node2host;
extern std::bitset<MAX_CLIENTS> local_nodes;
extern int glb_host_idx;
extern std::array<std::unique_ptr<RemoteChannel>, MAX_HOSTS> remote_channels;

extern long gettime_ns(int clock_id = CLOCK_MONOTONIC);

ReplayManager g_replay_mnger;

/* Caller must take the node_mutex. */
void ReplayManager::try_flush_delayed_msg(int dst_id)
{
    if (stage != STAGE_RESTORE && stage != STAGE_CONVERGE) {
        return;
    }
    std::vector<history_msg> &lis = delayed_msg_list_[dst_id];
    if (lis.size() == 0) {
        return;
    }
    msg_list_[dst_id].insert(
        msg_list_[dst_id].end(),
        std::make_move_iterator(lis.begin()),
        std::make_move_iterator(lis.end())
    );
    std::vector<history_msg>().swap(lis);
}

void ReplayManager::add_msg(std::shared_ptr<Message> &msg, int src_id, int dst_id)
{
    if (!local_nodes.test(dst_id)) {
        // send to remote host
        LOG("add_msg: %d => %d, send to remote\n", src_id, dst_id);
        int host_id = node2host[dst_id];
        auto* ch = remote_channels[host_id].get();
        ch->add_msg(msg);
        return;
    }
    std::unique_lock lock(node_mutex_[dst_id]);
    this->try_flush_delayed_msg(dst_id);
    real_hdr_t *hdr = (real_hdr_t *)msg->data();
    int bgp_type = BGP_TYPE((real_pld_t *)msg->data() + 1);
    if (stage == STAGE_CONVERGE || bgp_type == BGP_KEEPALIVE || bgp_type == BGP_OPEN) {
        msg_list_[dst_id].push_back({src_id, gettime_ns(), msg});
        LOG("add_msg: %d => %d, type %s, size %d, seq = %ld\n",
            src_id, dst_id, msg_type_name[hdr->msg_type], hdr->msg_len, msg_list_[dst_id].size());
    } else {
        delayed_msg_list_[dst_id].push_back({src_id, gettime_ns(), msg});
        LOG("delayed add_msg: %d => %d, type %s, size %d\n",
            src_id, dst_id, msg_type_name[hdr->msg_type], hdr->msg_len);
    }
}

bool ReplayManager::node_replay_one_msg(int node_id)
{
    dbg_assert(local_nodes.test(node_id),
        "node_replay_one_msg(): node2host[%d]=%d, glb_host_idx=%d\n", node_id, node2host[node_id], glb_host_idx);
    if (!glb_local_parts[iteration_idx].count(node_id) && !glb_local_cut.count(node_id)) {
        LOG("replay_one_msg(%d) failed because it's not online\n", node_id);
        return false;
    }

    std::unique_lock lock(node_mutex_[node_id]);
    if (stage == STAGE_RESTORE && replayed_seq_[node_id] == restore_until_seq_[node_id]) {
        LOG("replay_one_msg(%d) failed because it's already restored in STAGE_RESTORE\n", node_id);
        return false;
    }

    this->try_flush_delayed_msg(node_id);
    auto &lis = msg_list_[node_id];
    auto &seq = replayed_seq_[node_id];
    if (seq == msg_list_[node_id].size()) {
        // both zero, or both last replayable seq
        LOG("replay_one_msg(%d) failed because there's no message to replay\n", node_id);
        return false;
    }
    dbg_assert(seq < lis.size(), "node_id: %d, last_seq: %ld, siz: %d",
        node_id, seq, (int)lis.size());
    auto &hist = lis[seq]; // seq starts from 1, use 0 as default value is fine
    auto &msg = hist.msg;

    auto ch = g_channel_manager.get(node_id, hist.src_id);
    if (!ch || (ch->state() != Channel::CHANNEL_ESTABLISHED && ch->state() != Channel::BGP_ESTABLISHED)) {
        LOG("replay_one_msg(%d) failed because it's offline\n", node_id);
        return false;
    }
    dbg_assert(ch != nullptr, "invalid g_build_channel pointer at {%d, %d}\n", node_id, hist.src_id);

    // TODO: this asserts msg->data() conforms to align requirements of real_hdr_t, otherwise it's UB
    real_hdr_t *hdr = (real_hdr_t *)msg->data();
    // always replay BGP_OPEN and BGP_KEEPALIVE
    u_char bgp_type = BGP_TYPE((real_pld_t *)msg->data() + 1);
    if (bgp_type != BGP_OPEN && bgp_type != BGP_KEEPALIVE) {
        // if (stage == STAGE_TEARDOWN) {
        if (stage != STAGE_CONVERGE && stage != STAGE_RESTORE) {
            LOG("replay_one_msg(%d, bgp_type=%d) failed due to invalid stage\n", node_id, bgp_type);
            return false;
        }
    }
    if (!ch->bgp_is_established() && bgp_type == BGP_KEEPALIVE) {
        ch->on_bgp_established();
    }
    if (stage == STAGE_CONVERGE) {
        // receiving a new message (i.e. replayed a message in CONVERGE stage)
        // is enough to mark it as busy, even if it don't send message
        has_new_msg_ = true;
    }
    hdr->seq = seq + 1;
    ch->sendmsg(msg);

    seq++;
    LOG("replay_one_msg(%d), msg_list_len = %ld, src_id = %d, final seq = %ld\n",
        node_id, msg_list_[node_id].size(), hist.src_id, seq);
    return true;
}

void ReplayManager::export_iolog()
{
    std::ofstream iolog(logPath + "/io.log");
    std::vector<std::vector<history_msg>> src_msg_list(n_nodes + 1);
    for (auto &msgs : msg_list_) {
        for (auto msg : msgs) {
            src_msg_list[msg.src_id].push_back(msg);
        }
    }
    for (auto &lis : src_msg_list) {
        sort(lis.begin(), lis.end(), [](history_msg &h1, history_msg &h2) {
            return h1.timestamp < h2.timestamp;
        });
    }
    for (auto &msgs : src_msg_list) {
        long last_ts = 0;
        for (auto msg : msgs) {
            // only emit one line per 1ms
            if (msg.timestamp - last_ts < MSEC_PER_NS) {
                continue;
            }
            last_ts = msg.timestamp;
            iolog << std::format("{} {:.6f}\n", msg.src_id, msg.timestamp / 1e9);
        }
    }
}
