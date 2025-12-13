#include "channel.hpp"
#include "const.hpp"
#include "replay_manager.hpp"

#include <atomic>
#include <mutex>
#include <cassert>
#include <unordered_set>

extern volatile std::atomic<int> stage;

std::mutex Channel::port_mng_mutex;
std::atomic<int> Channel::n_channel = 0;
std::unordered_map<int, uint16_t> Channel::next_port;
std::unordered_map<std::pair<int,int>, uint16_t, pair_hash> Channel::port_store;

uint16_t Channel::alloc_port()
{
    int a = self_id_, b = peer_id_;
    if (a > b) {
        std::swap(a, b);
    }
    auto p = std::make_pair(a, b);
    std::unique_lock lock(port_mng_mutex);
    auto iter = port_store.find(p);
    if (iter != port_store.end()) {
        return iter->second;
    }
    if (next_port.count(b) == 0) {
        next_port[b] = PORT_START;
    }
    return port_store[p] = next_port[b]++;
}


Channel::Channel(int fd, int epfd, int self_id, int peer_id, uint32_t events, ChannelState init_state)
    :
        state_(init_state),
        fd_(fd),
        epfd_(epfd),
        self_id_(self_id),
        peer_id_(peer_id),
        events_(events),
        established_(false),
        rb_in_(RINGBUFFER_IN_SIZ),
        rb_out_(RINGBUFFER_OUT_SIZ)
{
    struct epoll_event ev = (struct epoll_event) {
        .events = events,
        .data = (union epoll_data) {.fd = fd}
    };
    int r = epoll_ctl(epfd_, EPOLL_CTL_ADD, fd_, &ev);
    LOG("epoll_ctl EPOLL_CTL_ADD r=%d, errno=%d, fd=%d, tid=%d\n", r, errno, fd_, tid);
    dbg_assert(r == 0, "EPOLL_CTL_ADD fd_=%d failed", fd_);
}

void Channel::sendmsg(std::shared_ptr<Message> &msg) {
    if (!(events_ & EPOLLOUT)) {
        events_ |= EPOLLOUT;
        struct epoll_event evt = (struct epoll_event) {
            .events = events_,
            .data = (union epoll_data) {.fd = fd_}
        };
        epoll_ctl(epfd_, EPOLL_CTL_MOD, fd_, &evt);
    }
    real_hdr_t *hdr = (real_hdr_t *)msg->data();
    real_hdr_t orig_hdr = *hdr;
    while (orig_hdr.msg_len > (int)rb_out_.capacity()) {
        rb_out_.expand();
    }
    assert(orig_hdr.msg_len <= (int)rb_out_.capacity());
    LOG("[%3d, %3d] sendmsg(): msg_type=%s, len=%d, seq=%ld\n",
        self_id_, peer_id_, msg_type_name[orig_hdr.msg_type], orig_hdr.msg_len, orig_hdr.seq);
    pending_out_msgs_.push(msg);
}

std::vector<std::shared_ptr<Message>> Channel::pollin() {
    LOG("[%3d, %3d] pollin\n", self_id_, peer_id_);

    std::vector<std::shared_ptr<Message>> incoming_msgs;
    auto n_read = rb_in_.readFromFd(fd_);
    LOG("read %ld bytes into ring buffer\n", n_read);

    while (rb_in_.availableRead()) {
        real_hdr_t hdr;
        bool r = rb_in_.peek(&hdr, hdrsiz);
        if (!r) {
            break;
        }
        size_t msglen = hdr.msg_len;
        while (msglen > rb_in_.capacity()) {
            rb_in_.expand();
        }
        if (rb_in_.availableRead() < msglen) {
            break;
        }
        assert(msglen <= rb_in_.capacity());
        auto msg = std::make_shared<Message>(msglen);
        msg->alloc_tail(msglen);
        r = rb_in_.get(msg->data(), msglen);
        assert(r);
        incoming_msgs.push_back(std::move(msg));
    }
    return incoming_msgs;
}

void Channel::on_connect_ok()
{
    dbg_assert(state_ == CONN_INPROGRESS,
        "expected CONN_INPROGRESS, actual state: %d", state_);
    state_ = CHANNEL_ESTABLISHED;

    auto resp_msg = std::make_shared<Message>(synsiz);
    real_syn_t *syn = (real_syn_t *)resp_msg->alloc_tail(synsiz);
    syn->hdr.msg_type = REAL_SYN;
    syn->hdr.msg_len = synsiz;
    syn->cli_id = peer_id_;
    syn->svr_id = self_id_;
    syn->cli_port = this->alloc_port();
    this->sendmsg(resp_msg);
}

void Channel::pollout() {
    LOG("[%3d, %3d] pollout\n", self_id_, peer_id_);
    dbg_assert(!pending_out_msgs_.empty(), "no pending msg in pollout()");

    // move messages into ring buffer
    while (!pending_out_msgs_.empty()) {
        std::shared_ptr<Message> curr_msg = pending_out_msgs_.front();
        if (curr_msg->len() <= (int)rb_out_.availableWrite()) {
            bool r = rb_out_.put(curr_msg->data(), curr_msg->len());
            assert(r);
        } else {
            LOG("break\n");
            break;
        }
        LOG("add message len %d\n", (int)curr_msg->len());
        pending_out_msgs_.pop();
    }

    /* actually sending message */
    int n_bytes = rb_out_.writeToFd(fd_);
    dbg_assert(n_bytes > 0, "writev: n_bytes = %d, availableRead = %d\n", n_bytes, (int)rb_out_.availableRead());
    rb_out_.consume(n_bytes);
    if (!pending_out_msgs_.empty() || rb_out_.availableRead()) {
        return;
    }
    // no message to send, disable EPOLLOUT
    // LOG("Disabling EPOLLOUT()\n");
    events_ &= ~EPOLLOUT;
    struct epoll_event ev = (struct epoll_event) {
        .events = events_,
        .data = (union epoll_data) {.fd = fd_}
    };
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd_, &ev);
    // LOG("Leaving pollout()\n");
}

bool Channel::pollerr(int event) {
    LOG("[%3d, %3d] pollerr\n", self_id_, peer_id_);
    assert(event & (EPOLLHUP | EPOLLERR));
    return true;
}

Channel::~Channel()
{
    int r = epoll_ctl(epfd_, EPOLL_CTL_DEL, fd_, NULL);
    LOG("epoll_ctl EPOLL_CTL_DEL r=%d, errno=%d, fd=%d, tid=%d\n", r, errno, fd_, tid);
    if (r != 0) {
        LOG("epoll del failed: %s\n", strerror(errno));
    }
    close(this->fd_);
    if (established_) {
        // TODO: can this happen in STAGE_BUILDUP?
        assert(stage != STAGE_RESTORE && stage != STAGE_CONVERGE);
        int nchannel_old = n_channel--;
        int nchannel_new = nchannel_old - 1;
        LOG("n_channel %d => %d\n", nchannel_old, nchannel_new);
    }
}

void Channel::on_bgp_established()
{
    dbg_assert(state_ == CHANNEL_ESTABLISHED,
        "expected CHANNEL_ESTABLISHED, actual state: %d", state_);
    state_ = BGP_ESTABLISHED;

    assert(stage == STAGE_BUILDUP && !established_);
    int nchannel_old = n_channel++;
    int nchannel_new = nchannel_old + 1;
    LOG("[%3d, %3d] fd=%d register, n_channel %d => %d\n", self_id_, peer_id_, fd_, nchannel_old, nchannel_new);
    established_ = true;
}

void Channel::on_receive_syn() {
    dbg_assert(state_ == ACCEPTED,
        "expected ACCEPTED, actual state: %d", state_);
    state_ = CHANNEL_ESTABLISHED;
    /* send SYNACK */
    auto resp_msg = std::make_shared<Message>(synacksiz);
    real_synack_t *synack = (real_synack_t *)resp_msg->alloc_tail(synacksiz);
    synack->hdr.msg_type = REAL_SYNACK;
    synack->hdr.msg_len = synacksiz;
    synack->hdr.seq = 0;
    synack->cli_port = this->alloc_port();

    this->sendmsg(resp_msg);
}
