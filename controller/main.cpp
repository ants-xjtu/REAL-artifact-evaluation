#include "debug.hpp"
#include "node_ops.hpp"
#include "const.hpp"
#include "channel.hpp"
#include "channel_manager.hpp"
#include "replay_manager.hpp"
#include "remote_worker.hpp"

#include "json.hpp"
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <set>
#include <format>
#include <bitset>

using json = nlohmann::json;

extern "C" {
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <signal.h>
#include <execinfo.h>
}

thread_local static struct epoll_event events[MAX_CONNS];

/* debug.hpp */
FILE *log_file[128];
thread_local int tid;

/* partition scheduling */
std::unordered_set<int> idle_parts;
volatile std::atomic<int> stage = STAGE_BUILDUP;
volatile std::atomic<int> n_ready_host = 0;
int iteration_round = 0;
int iteration_idx = 0;
int iteration_delta = 1;

/* topology */
std::unordered_set<int> glb_all_cut;
std::unordered_set<int> glb_local_cut;
std::unordered_set<int> glb_seen_nodes;
std::vector<std::unordered_set<int>> glb_all_parts;
std::vector<std::unordered_set<int>> glb_local_parts;
std::vector<std::vector<int>> glb_G;
std::vector<int> glb_parts_nchannel;
std::vector<int> glb_parts_nchannel_cut;
std::unordered_map<int, std::string> neighborList;
int n_parts;
int n_nodes;

/* config */
std::string logPath;
std::string image;
std::string conf;
int nthreads;

/* distributed */
int glb_nhosts;
int glb_host_idx;
std::bitset<MAX_CLIENTS> local_nodes;
std::array<int, MAX_CLIENTS> node2host;

/* ctrl pipe */
int ctrl_pipe[MAX_THREADS + 1][2];
int ctrl_rev_pipe[MAX_THREADS + 1][2];
std::mutex worker_ctrl_pipe_mutex[MAX_THREADS + 1];

int remote_ctrl_rev_pipe[MAX_THREADS + 1][2];
extern std::array<std::unique_ptr<RemoteChannel>, MAX_HOSTS> remote_channels;

static std::string get_stage_name() {
    int curr_stage = stage.load();
    switch (curr_stage) {
    case STAGE_BUILDUP:
    case STAGE_RESTORE:
    case STAGE_CONVERGE:
    case STAGE_TEARDOWN:
    case STAGE_END:
        return stage_name[curr_stage];
    default:
        dbg_assert(0, "Unknown stage: %d", curr_stage);
        return "";
    }
}

long gettime_ns(int clock_id = CLOCK_MONOTONIC) {
    struct timespec ts;
    clock_gettime(clock_id, &ts);
    return ts.tv_sec * 1'000'000'000 + ts.tv_nsec;
}

static int read_int(int fd) {
    int ans;
    int r = read(fd, &ans, sizeof(ans));
    assert(r > 0);
    return ans;
}

static void write_int(int fd, int num) {
    int r = write(fd, &num, sizeof(num));
    assert(r == sizeof(num));
    return;
}

int init_socket()
{
    int sockfd;
    int r;
    struct sockaddr_un addr;

    // Create Unix domain socket
    sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    dbg_assert(sockfd >= 0, "Socket creation failed");
    int msg_manager_socket = sockfd;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, MNG_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(MNG_SOCKET_PATH);
    // Bind socket
    r = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    dbg_assert(r == 0, "bind(%d, %s) failed", sockfd, MNG_SOCKET_PATH);

    r = chmod(MNG_SOCKET_PATH, 0666);
    dbg_assert(r == 0, "chmod(%s, 0666) failed", MNG_SOCKET_PATH);

    // Listen for connection requests
    r = listen(sockfd, MAX_CONNS);
    dbg_assert(r == 0, "Listen failed");

    return msg_manager_socket;
}

#ifdef ITER_CONV
static std::vector<std::unordered_set<int>> parse_parts(const std::string &topoPath)
{
    std::string partFilename = topoPath + "/partition.json";
    json part_json;
    std::ifstream partFile(partFilename);
    if (!partFile.is_open()) {
        throw std::runtime_error("Cannot open file: " + partFilename);
    }
    partFile >> part_json;
    std::vector<std::unordered_set<int>> parts(part_json.size());
    for (size_t i = 0; i < part_json.size(); ++i) {
        auto &part = part_json[i];
        for (auto u : part) {
            parts[i].insert(u.get<int>());
        }
    }
    return parts;
}
#endif

static std::tuple<
    std::vector<std::vector<int>>, // G
    std::vector<std::unordered_set<int>>, // all_parts
    std::vector<std::unordered_set<int>>, // local_parts
    std::vector<int>, // parts_nchannel
    std::vector<int>, // parts_nchannel_cut
    std::unordered_map<int, std::string>, // neighbor list
    std::vector<std::unordered_set<int>> // host_nodes
> parse_topo(const std::string &topoPath, int nhosts, int self_host_idx) {
    // G
    std::string topoFilename = topoPath + "/blueprint.json";

    json topo;
    std::ifstream topoFile(topoFilename);
    if (!topoFile.is_open()) {
        throw std::runtime_error("Cannot open file: " + topoFilename);
    }
    topoFile >> topo;

    size_t n_nodes = topo["routers"].size();
    std::vector<std::vector<int>> G(n_nodes + 1);
    for (auto &r : topo["routers"]) {
        int u = r["idx"].get<int>();
        for (auto &neigh : r["neighbors"]) {
            int v = neigh["peeridx"].get<int>();
            G[u].push_back(v);
            // (v, u) will be read again
        }
    }

    // parts & parts_nchannel
#ifdef ITER_CONV
    std::vector<std::unordered_set<int>> all_parts = parse_parts(topoPath);
#else
    std::vector<std::unordered_set<int>> all_parts(1);
    for (size_t u = 1; u <= n_nodes; ++u) {
        all_parts[0].insert(u);
    }
#endif
    int n_parts = all_parts.size();
    std::vector<std::set<int>> all_parts_sorted;
    for (int i = 0; i < n_parts; ++i) {
        all_parts_sorted.emplace_back(all_parts[i].begin(), all_parts[i].end());
    }
    std::vector<std::unordered_set<int>> host_nodes(nhosts);
    std::vector<std::unordered_set<int>> local_parts(n_parts);
    for (int i = 0; i < n_parts; ++i) {
        int part_size = all_parts_sorted[i].size();
        int perhost_size = (part_size + nhosts - 1) / nhosts;
        int host_idx = 0, currhost_size = 0;
        // ensure the same order on different machine
        for (auto u : all_parts_sorted[i]) {
            assert(host_idx < nhosts);
            host_nodes[host_idx].insert(u);
            if (host_idx == self_host_idx) {
                local_parts[i].insert(u);
            }
            currhost_size++;
            if (currhost_size >= perhost_size) {
                host_idx++;
                currhost_size = 0;
            }
        }
    }
    std::vector<int> parts_nchannel(n_parts);
    for (int i = 0; i < n_parts; ++i) {
        parts_nchannel[i] = 0;
        for (auto u : local_parts[i]) {
            parts_nchannel[i] += topo["routers"][u - 1]["neighbors"].size();
        }
    }
    std::vector<int> parts_nchannel_cut(n_parts);
    auto &cut = local_parts[n_parts - 1];
    std::unordered_set<int> up_nodes(cut.begin(), cut.end());
    for (int i = 0; i < n_parts - 1; ++i) {
        parts_nchannel_cut[i] = 0;
        // NOTE: use all_parts rather than local_parts, local cut to remote node counts
        up_nodes.insert(all_parts[i].begin(), all_parts[i].end());
        for (auto u : cut) {
            for (auto neigh : topo["routers"][u - 1]["neighbors"]) {
                if (up_nodes.count(neigh["peeridx"])) {
                    parts_nchannel_cut[i]++;
                }
            }
        }
    }

    // neighborList
    std::unordered_map<int, std::string> neighbor_dict;

    for (auto& router : topo["routers"]) {
        int idx = router["idx"];
        std::string neighbor_str;
        for (auto& neigh : router["neighbors"]) {
            neighbor_str += neigh["self_ip"].get<std::string>() + ":" +
                            neigh["neighbor_ip"].get<std::string>() + ":" +
                            std::to_string(neigh["peeridx"].get<int>()) + ",";
        }
        neighbor_dict[idx] = neighbor_str;
    }

    return {G, all_parts, local_parts, parts_nchannel, parts_nchannel_cut, neighbor_dict, host_nodes};
}

std::string get_addr(int id, uint16_t port) {
    return "/opt/lwc/volumes/ripc/emu-real-" + std::to_string(id) + "/listener:" + std::to_string(port);
}

inline bool globally_converged() {
    return (int)idle_parts.size() == n_parts;
}

void end_iteration(
    std::vector<std::unordered_set<int>> &parts,
    std::unordered_map<int, std::string> &neighborList,
    std::string &log_path
)
{
    g_replay_mnger.new_iteration();
    static int tag = 1;
    if (globally_converged()) {
        export_routes(image, parts[iteration_idx], "final", log_path);
        export_routes(image, glb_local_cut, "final", log_path);
    } else {
        export_routes(image, parts[iteration_idx], std::to_string(tag), log_path);
        export_routes(image, glb_local_cut, std::to_string(tag), log_path);
    }
    tag++;

    std::string ts_filename = log_path + "/switch_pods_ts.txt";
    FILE *TsFile = fopen(ts_filename.c_str(), "a+");
    fprintf(TsFile, "%.6f\n", (double)gettime_ns() / 1e9);
    fclose(TsFile);

    stop_nodes(image, parts[iteration_idx], log_path);
    for (auto u : parts[iteration_idx]) {
        g_replay_mnger.node_offline(u);
    }
}

static int cut_nchannel()
{
    if (iteration_round == 0) {
        return glb_parts_nchannel_cut[iteration_idx];
    } else {
        return glb_parts_nchannel[n_parts];
    }
}

static int get_nchannel_tgt()
{
    int ret = glb_parts_nchannel[iteration_idx];
    if (n_parts > 0) {
        ret += cut_nchannel();
    }
    return ret;
}

void try_buildup()
{
    std::vector<std::tuple<int, int, int, int, int>> fd_pass_list;
    std::unordered_set<int> nodes = glb_local_parts[iteration_idx];
    for (auto u : glb_local_cut) {
        nodes.insert(u);
    }
    for (auto i : nodes) { // i is definitely local node
        // i is online, check (i <= ctrler) <= j channel
        int i_is_cut = glb_all_cut.count(i);
        for (auto j : glb_G[i]) { // i and j are connected
            // j don't need to be local node, which implies:
            // 1. don't skip j for being remote
            // 2. use glb_all_cut rather than glb_local_cut here
            int j_is_cut = glb_all_cut.count(j);
            if (i_is_cut == j_is_cut) {
                // equivalent nodes, break tie by node_id
                if (i < j) {
                    // i will build the channel actively
                    continue;
                }
            } else {
                // normal node --(ctrl)--> cut
                // only do ctrl->cut part, node->ctrl part is handled elsewhere
                // 1. avoid the reversed direction, i.e. ctrl -> normal node
                if (!i_is_cut) {
                    continue;
                }
                // 2. if normal node is unseen, don't build (ctrl)-->cut channel
                if (!glb_seen_nodes.count(j)) {
                    continue;
                }
            }
            if (g_channel_manager.get(i, j)) {
                continue;
            }
            // channel is not built
            int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            struct sockaddr_un addr = { .sun_family = AF_UNIX };
            std::string addr_path = get_addr(i, 179);
            strncpy(addr.sun_path, addr_path.c_str(), sizeof(addr.sun_path) - 1);

            int r = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

            fd_pass_list.push_back({fd, i, j, r, errno});

            LOG("[%3d, %3d] building channel: fd=%d, connect(addr=%s) = %d, errno = %d\n",
                i, j, fd, addr_path.c_str(), r, errno);
        }
    }

    for (auto [fd, i, j, r, err] : fd_pass_list) {
        int cmd = 0;
        int worker_id = i % nthreads;
        int ctrl_fd = ctrl_pipe[worker_id][1];
        LOG("pass %d to thread %d\n", fd, worker_id);
        std::unique_lock lock(worker_ctrl_pipe_mutex[worker_id]);
        write_int(ctrl_fd, cmd);
        write_int(ctrl_fd, fd);
        write_int(ctrl_fd, i);
        write_int(ctrl_fd, j);
        write_int(ctrl_fd, r);
        write_int(ctrl_fd, err);
    }
}

void stage_transition(bool has_event)
{
    static long last_conn_ts = 0;
    static long last_event_ts = 0;
    static long last_teardown_debug_ts = 0;
    static long last_keepbusy_ts = 0;
    if (has_event) {
        last_event_ts = gettime_ns();
    }
    static bool local_stage_end = false;
    if (stage == STAGE_CONVERGE && last_event_ts - last_keepbusy_ts > KEEPBUSY_INTERVAL) {
        last_keepbusy_ts = last_event_ts;
        for (int hid = 0; hid < glb_nhosts; ++hid) {
            if (remote_channels[hid] != nullptr) {
                LOG("send_keepbusy %s host %d\n", stage_name[stage], hid);
                remote_channels[hid]->send_keepbusy();
            }
        }
    }
    switch (stage) {
    case STAGE_BUILDUP: {
        if (!local_stage_end) {
            /* Scan for unbuilt channels once a second */
            if (last_conn_ts != 0 && gettime_ns() - last_conn_ts < BUILDUP_TRY_INTERVAL) {
                return;
            }
            last_conn_ts = gettime_ns();
            try_buildup();
            int nch_target = get_nchannel_tgt();
            LOG("n_channel: %d, tgt: %d\n", (int)(Channel::n_channel), nch_target);
            std::cout << "n_channel: " << Channel::n_channel << ", tgt: " << nch_target << std::endl;
            if (Channel::n_channel < nch_target) {
                break;
            }
            assert(Channel::n_channel == nch_target);
            local_stage_end = true;
            n_ready_host++;
            for (int hid = 0; hid < glb_nhosts; ++hid) {
                if (remote_channels[hid] != nullptr) {
                    LOG("send_eos %s host %d\n", stage_name[stage], hid);
                    remote_channels[hid]->send_eos(stage);
                }
            }
            std::cout << std::format("{:.6f}: {} done locally", gettime_ns() / 1e9, get_stage_name()) << std::endl;
        }
        if (local_stage_end && n_ready_host == glb_nhosts) {
            local_stage_end = false;
            n_ready_host = 0;
            if (iteration_round == 0) {
                stage = STAGE_CONVERGE;
            } else {
                stage = STAGE_RESTORE;
            }
            std::cout << std::format("{:.6f}: {}", gettime_ns() / 1e9, get_stage_name()) << std::endl;
            last_event_ts = gettime_ns(); // timeout should be counted as least from now.   
        }
        break;
    }
    case STAGE_RESTORE: {
        if (!local_stage_end) {
            if (gettime_ns() - last_event_ts < CONVERGE_TIMEOUT) {
                break;
            }
            local_stage_end = true;
            n_ready_host++;
            for (int hid = 0; hid < glb_nhosts; ++hid) {
                if (remote_channels[hid] != nullptr) {
                    LOG("send_eos %s host %d\n", stage_name[stage], hid);
                    remote_channels[hid]->send_eos(stage);
                }
            }
            std::cout << std::format("{:.6f}: {} done locally", gettime_ns() / 1e9, get_stage_name()) << std::endl;
        }
        if (local_stage_end && n_ready_host == glb_nhosts) {
            local_stage_end = false;
            n_ready_host = 0;
            stage = STAGE_CONVERGE;
            std::cout << std::format("{:.6f}: {}", gettime_ns() / 1e9, get_stage_name()) << std::endl;
            last_event_ts = gettime_ns(); // timeout should be counted as least from now.
        }
        break;
    }
    case STAGE_CONVERGE: {
        if (!local_stage_end) {
            if (gettime_ns() - last_event_ts < CONVERGE_TIMEOUT) {
                break;
            }
            local_stage_end = true;
            n_ready_host++;
            for (int hid = 0; hid < glb_nhosts; ++hid) {
                if (remote_channels[hid] != nullptr) {
                    LOG("send_eos %s host %d\n", stage_name[stage], hid);
                    remote_channels[hid]->send_eos(stage);
                }
            }
            std::cout << std::format("{:.6f}: {} done locally", gettime_ns() / 1e9, get_stage_name()) << std::endl;
        }
        if (local_stage_end && n_ready_host == glb_nhosts) {
            local_stage_end = false;
            n_ready_host = 0;
            stage = STAGE_TEARDOWN;
            std::cout << std::format("{:.6f}: {}", gettime_ns() / 1e9, get_stage_name()) << std::endl;
            if (g_replay_mnger.has_new_msg()) {
                std::cout << std::format("{:.6f}: part {} busy", gettime_ns() / 1e9, iteration_idx) << std::endl;
                idle_parts.clear();
            } else {
                std::cout << std::format("{:.6f}: part {} idle", gettime_ns() / 1e9, iteration_idx) << std::endl;
                idle_parts.insert(iteration_idx);
            }
            std::string ts_path = logPath + "/converge_end_ts.txt";
            FILE *end_ts_file = fopen(ts_path.c_str(), "aw");
            fprintf(end_ts_file, "%.6f\n", (double)last_event_ts / 1e9);
            fclose(end_ts_file);
            end_iteration(glb_local_parts, neighborList, logPath);
        }
        break;
    }
    case STAGE_TEARDOWN: {
        if (!local_stage_end) {
            if (gettime_ns() - last_teardown_debug_ts > 1'000'000'000) {
                last_teardown_debug_ts = gettime_ns();
                std::cout << "n_channel: " << Channel::n_channel << ", cut_nchannel: " << cut_nchannel() << std::endl;
            }
            if (Channel::n_channel > cut_nchannel()) {
                break;
            }
            dbg_assert(Channel::n_channel == cut_nchannel(),
                "cut_nchannel %d, actual nchannel %d", cut_nchannel(), Channel::n_channel.load());
            local_stage_end = true;
            n_ready_host++;
            for (int hid = 0; hid < glb_nhosts; ++hid) {
                if (remote_channels[hid] != nullptr) {
                    LOG("send_eos %s host %d\n", stage_name[stage], hid);
                    remote_channels[hid]->send_eos(stage);
                }
            }
            std::cout << std::format("{:.6f}: {} done locally", gettime_ns() / 1e9, get_stage_name()) << std::endl;
        }
        if (local_stage_end && n_ready_host == glb_nhosts) {
            local_stage_end = false;
            n_ready_host = 0;
            if (globally_converged()) {
                stage = STAGE_END;
                std::cout << std::format("{:.6f}: {}", gettime_ns() / 1e9, get_stage_name()) << std::endl;
                break;
            }
            // local converge, switch to next part
            do {
                iteration_idx += iteration_delta;
                if (iteration_idx >= n_parts) {
                    iteration_round++;
                    iteration_delta = -1;
                    iteration_idx -= 2;
                } else if (iteration_idx < 0) {
                    iteration_round++;
                    iteration_delta = 1;
                    iteration_idx += 2;
                }
            } while (idle_parts.count(iteration_idx));
            for (auto u : glb_all_parts[iteration_idx]) {
                glb_seen_nodes.insert(u);
            }
            stage = STAGE_BUILDUP;
            std::cout << std::format("{:.6f}: {} @ part {}", gettime_ns() / 1e9, get_stage_name(), iteration_idx) << std::endl;
            if (iteration_round == 0) {
                start_nodes(image, glb_local_parts[iteration_idx], neighborList, neighborList.size(), logPath);
                std::cout << std::format("{:.6f}: start_nodes done", gettime_ns() / 1e9) << std::endl;
            } else {
                restart_nodes(image, glb_local_parts[iteration_idx], neighborList, neighborList.size(), logPath);
                std::cout << std::format("{:.6f}: restart_nodes done", gettime_ns() / 1e9) << std::endl;
            }
        }
        break;
    }
    default: {
        dbg_assert(0, "Unknown stage: %d\n", stage.load());
        break;
    }
    }
}

static inline bool allow_connect(int src, int dst)
{
    bool ok = true;
    /**
     * Only allow connection of a determined direction:
     * (1) if both cut or both normal nodes, only allow src < dst
     *    src->ctrl is handled here, ctrl->dst is handled in try_buildup()
     * (2) if one cut and one normal node, only allow normal to cut
     *    node->ctrl is handled here, ctrl->cut is handled in try_buildup()
     * NOTE:
     * If the normal node is never up, the connect() must be made by cut,
     * and (2) implies that such connection should be rejected.
     * This is clever in that it doesn't check seen_nodes.
     * This effectively delayes cut<->part connection until the part's first bootup
     */
    int src_is_cut = glb_all_cut.count(src);
    int dst_is_cut = glb_all_cut.count(dst);
    if (src_is_cut != dst_is_cut) {
        if (src_is_cut) {
            // cut -> normal, reject
            ok = false;
        }
    } else if (src > dst) {
        ok = false;
    }
    return ok;
}

void worker_main(int worker_id)
{
    tid = worker_id;

    int timeout = 200; // ms
    int epfd = epoll_create(MAX_CONNS);
    if (epfd < 0) {
        perror("epoll_create failed");
        return;
    }
    int ctrl_fd = ctrl_pipe[tid][0];
    int ctrl_rev_fd = ctrl_rev_pipe[tid][1];
    struct epoll_event ev = (struct epoll_event) {
        .events = EPOLLIN,
        .data = (union epoll_data) {
            .fd = ctrl_fd
        }
    };
    epoll_ctl(epfd, EPOLL_CTL_ADD, ctrl_fd, &ev);

    std::unordered_set<int> managed_nodes;
    for (int i = worker_id ?: nthreads; i <= n_nodes; i += nthreads) {
        if (local_nodes.test(i)) {
            managed_nodes.insert(i);
        }
    }

    int nev;
    while ((nev = epoll_wait(epfd, events, MAX_CONNS, timeout)) >= 0) {
        LOG("epoll_wait() = %d\n", nev);
        bool external_event = false;
        /* Process Events */
        for (int i = 0; i < nev; ++i) {
            int event = events[i].events;
            int fd = events[i].data.fd;
            if (fd == ctrl_fd) {
                int cmd = read_int(ctrl_fd);
                switch (cmd) {
                case 0: { // active ch_fd: controller connect()
                    int ch_fd = read_int(ctrl_fd);
                    int i = read_int(ctrl_fd);
                    int j = read_int(ctrl_fd);
                    int connect_res = read_int(ctrl_fd);
                    int connect_errno = read_int(ctrl_fd);
                    LOG("recv active %d @ thread %d\n", ch_fd, worker_id);

                    if (connect_res == 0) {
                        LOG("[%3d, %3d] connect(fd=%d, self_id=%d, peer_id=%d) success\n",
                            i, j, ch_fd, i, j);
                        auto channel = g_channel_manager.make_channel(ch_fd, epfd, i, j, EPOLLIN | EPOLLOUT, Channel::CONN_INPROGRESS);
                        channel->on_connect_ok();
                    } else if (connect_res == -1 && connect_errno != EAGAIN) {
                        LOG("[%3d, %3d] connect(fd=%d, self_id=%d, peer_id=%d) error: %d (%s)\n",
                            i, j, ch_fd, i, j, connect_res, strerror(connect_errno));
                    } else {
                        LOG("[%3d, %3d] connect(fd=%d, self_id=%d, peer_id=%d) EAGAIN\n",
                            i, j, ch_fd, i, j);
                        assert(connect_errno == EAGAIN);
                        g_channel_manager.make_channel(ch_fd, epfd, i, j, EPOLLIN | EPOLLOUT, Channel::CONN_INPROGRESS);
                    }
                    break;
                }
                case 1: { // passive ch_fd: controller accept()
                    int ch_fd = read_int(ctrl_fd);
                    int u = read_int(ctrl_fd);
                    int v = read_int(ctrl_fd);
                    LOG("accept() = %d (%d -> %d)\n", ch_fd, u, v);
                    assert(ch_fd >= 0);
                    g_channel_manager.make_channel(ch_fd, epfd, u, v, EPOLLIN, Channel::ACCEPTED);
                    LOG("recv passive %d @ thread %d\n", ch_fd, worker_id);
                    break;
                }
                case 2: { // shutdown
                    return;
                }
                default: {
                    LOG("cmd=%d\n", cmd);
                    assert(0);
                }
                }
                continue;
            }
            external_event = true;
            auto channel = g_channel_manager.get_by_fd(fd);
            dbg_assert(channel != nullptr, "channel @ fd(%d) == nullptr", fd);
            if (event & (~(EPOLLIN | EPOLLOUT))) {
                LOG("fd=%d pollerr()\n", fd);
                bool destroy = channel->pollerr(event & (~(EPOLLIN | EPOLLOUT)));
                if (destroy) {
                    g_channel_manager.delete_channel(fd);
                }
                continue;
            }
            if (channel->state() == Channel::CONN_INPROGRESS) {
                assert(event & EPOLLOUT);
                LOG("outcoming connect() success, fd=%d, event = %x\n", fd, event);
                // out coming connect()s
                if (event & (~(EPOLLIN | EPOLLOUT))) {
                    g_channel_manager.delete_channel(fd);
                } else {
                    channel->on_connect_ok();
                }
                continue;
            }
            if (event & EPOLLIN) {
                LOG("fd=%d pollin()\n", fd);
                std::vector<std::shared_ptr<Message>> incoming_msgs = channel->pollin();
                for (auto &msg : incoming_msgs) {
                    real_hdr_t *hdr = (real_hdr_t *)msg->data();
                    real_hdr_t orig_hdr = *hdr;
                    switch (hdr->msg_type) {
                    case REAL_SYN: {
                        // ignore syn->cli_port, it's only used in
                        // listener's accept() in the shim
                        channel->on_receive_syn();
                        break;
                    }
                    case REAL_PAYLOAD: {
                        // round 0: both stage buildup and converge can add_msg
                        if (iteration_round == 0 && stage == STAGE_TEARDOWN) {
                            break;
                        }
                        // round 1+: only stage converge can add_msg, otherwise treat as restore
                        if (iteration_round != 0 && stage != STAGE_CONVERGE) {
                            break;
                        }
                        g_replay_mnger.add_msg(msg, channel->self_id(), channel->peer_id());
                        break;
                    }
                    case REAL_SYNACK: {
                        dbg_assert(false, "Manager don't receive REAL_SYNACK packet.");
                        break;
                    }
                    default:
                        dbg_assert(false, "Unexpected msg_type: %d", hdr->msg_type);
                        break;
                    }
                    LOG("[%3d, %3d] recvmsg(): msg_type=%s(%d), len=%d, seq=%ld\n",
                        channel->self_id(), channel->peer_id(), msg_type_name[orig_hdr.msg_type],
                        orig_hdr.msg_type, orig_hdr.msg_len, orig_hdr.seq);
                }
            }
            if (event & EPOLLOUT) {
                LOG("fd=%d pollout()\n", fd);
                channel->pollout();
            }
        }
        if (stage != STAGE_TEARDOWN) {
        // if (stage == STAGE_CONVERGE || stage == STAGE_RESTORE) {
            for (auto nid : managed_nodes) {
                g_replay_mnger.node_replay_one_msg(nid);
            }
        }
        if (external_event) {
            write_int(ctrl_rev_fd, 1);
        }
    }
}

void acceptor_main(int thread_id, int msg_manager_socket)
{
    tid = thread_id;

    int timeout = 200; // ms
    int epfd = epoll_create(MAX_CONNS);
    if (epfd< 0) {
        perror("epoll_create failed");
        return;
    }
    struct epoll_event ev = {
        .events = EPOLLIN,
        .data = (union epoll_data) {
            .fd = msg_manager_socket
        }
    };
    epoll_ctl(epfd, EPOLL_CTL_ADD, msg_manager_socket, &ev);

    int acceptor_ctrl_fd = ctrl_pipe[tid][0];
    ev.data.fd = acceptor_ctrl_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, acceptor_ctrl_fd, &ev);

    int nev;
    while ((nev = epoll_wait(epfd, events, MAX_CONNS, timeout)) >= 0) {
        LOG("epoll_wait() = %d\n", nev);
        for (int i = 0; i < nev; ++i) {
            int event = events[i].events;
            int fd = events[i].data.fd;
            if (fd != msg_manager_socket) {
                if (fd == acceptor_ctrl_fd) {
                    int r = read_int(acceptor_ctrl_fd);
                    assert(r == 2);
                    return;
                }
                // rejected ch_fd
                LOG("fd %d, event %x\n", fd, event);
                if ((event & (~(EPOLLIN | EPOLLOUT))) == 0) {
                    continue;
                }
                assert(0 == epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL));
                close(fd);
                continue;
            }
            assert(fd == msg_manager_socket);
            assert(event == EPOLLIN);
            // 1. accept()
            // 2. parse the peer id according to uds address
            // 3. monitor the new fd_ epoll_ctl
            struct sockaddr_un uds_srcaddr;
            socklen_t socklen = sizeof(uds_srcaddr);
            int ch_fd = accept4(msg_manager_socket, (sockaddr*)&uds_srcaddr, &socklen, SOCK_CLOEXEC);
            int u, v;
            dbg_assert(ch_fd >= 0, "errno: %d, sunpath = %s, socklen=%d", errno, uds_srcaddr.sun_path, socklen);
            sscanf(uds_srcaddr.sun_path, "/ripc/emu-real-%d/%d", &u, &v);
            LOG("accept() = %d (%d -> %d)\n", ch_fd, u, v);
            if (!!g_channel_manager.get(u, v) || !allow_connect(u, v)) {
                real_syn_t syn;
                int n_read = 0;
                do {
                    // TODO: don't block here
                    int r = read(ch_fd, ((char *)&syn) + n_read, synsiz - n_read);
                    assert(r > 0);
                    n_read += r;
                } while (n_read < synsiz);
                real_synack_t synack;
                synack.hdr.msg_type = REAL_SYNACK;
                synack.hdr.msg_len = synacksiz;
                synack.hdr.seq = 0;
                synack.cli_port = 0;
                int n_written = 0;
                do {
                    // TODO: don't block here
                    int r = write(ch_fd, ((char *)&synack) + n_written, synacksiz - n_written);
                    assert(r > 0);
                    n_written += r;
                } while (n_written < synacksiz);
                LOG("rejected\n");
                ev.data.fd = ch_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, ch_fd, &ev);
                continue;
            }
            int flags = fcntl(ch_fd, F_GETFL, 0);
            fcntl(ch_fd, F_SETFL, flags | O_NONBLOCK);
            int worker_id = u % nthreads;
            LOG("pass %d to thread %d\n", ch_fd, worker_id);
            int worker_ctrl_fd = ctrl_pipe[worker_id][1];
            std::unique_lock lock(worker_ctrl_pipe_mutex[worker_id]);
            write_int(worker_ctrl_fd, 1);
            write_int(worker_ctrl_fd, ch_fd);
            write_int(worker_ctrl_fd, u);
            write_int(worker_ctrl_fd, v);
        }
    }
}

int main(int argc, char *argv[]) {
    int msg_manager_socket = init_socket();

    int timeout = 200; // ms
    long max_runtime_ns = 0;

    image = argv[1];
    conf = argv[2];
    logPath = argv[3];
    nthreads = stoi(std::string(argv[4]));
    max_runtime_ns = stoi(std::string(argv[5])) * 1'000'000'000L;
    auto [nhosts, host_idx] = inter_host_buildup(argv[6]);
    glb_nhosts = nhosts;
    glb_host_idx = host_idx;

    std::string topoPath = "conf/" + image + "/" + conf + "/";
    auto [G_, all_parts_, local_parts_, parts_nchannel_, parts_nchannel_cut_, neighborList_, host_nodes] = parse_topo(topoPath, nhosts, host_idx);
    glb_G = G_; glb_all_parts = all_parts_; glb_local_parts = local_parts_;
    glb_parts_nchannel = parts_nchannel_; glb_parts_nchannel_cut = parts_nchannel_cut_; neighborList = neighborList_;
    n_parts = all_parts_.size() - 1;
    n_nodes = glb_G.size() - 1;
    assert(n_parts != 1);

    for (auto u : host_nodes[host_idx]) {
        local_nodes.set(u);
    }

    for (int hid = 0; hid < nhosts; ++hid) {
        for (auto u : host_nodes[hid]) {
            node2host[u] = hid;
        }
    }

    n_parts = local_parts_.size() - 1;
    assert(n_parts != 1);

    if (n_parts > 0) {
        glb_local_cut = glb_local_parts[n_parts];
        glb_all_cut = glb_all_parts[n_parts];
    }

    g_replay_mnger.init(n_nodes);
    g_channel_manager.init(n_nodes);

    LOG("=========Topo Debug ==========\n");
    LOG("G:\n");
    for (size_t u = 0; u < glb_G.size(); ++u) {
        for (auto v : glb_G[u]) {
            LOG("%3d => %3d\n", (int)u, v);
        }
    }
    LOG("host_nodes:\n");
    for (int h = 0; h < nhosts; ++h) {
        std::string host_str = "host_nodes[" + std::to_string(h) + "] = [";
        for (auto u : host_nodes[h]) {
            host_str += std::to_string(u) + " ";
        }
        host_str += "]";
        LOG("%s\n", host_str.c_str());
    }
    LOG("all_parts:\n");
    int part_idx = 0;
    for (auto part : glb_all_parts) {
        std::string part_str = "all_parts[" + std::to_string(part_idx) + "] = [";
        for (auto u : part) {
            part_str += std::to_string(u) + " ";
        }
        part_str += "]";
        LOG("%s\n", part_str.c_str());
        part_idx++;
    }
    LOG("local_parts:\n");
    part_idx = 0;
    for (auto part : glb_local_parts) {
        std::string part_str = "local_parts[" + std::to_string(part_idx) + "] = [";
        for (auto u : part) {
            part_str += std::to_string(u) + " ";
        }
        part_str += "]";
        LOG("%s\n", part_str.c_str());
        part_idx++;
    }
    LOG("parts_nchannel:\n");
    part_idx = 0;
    for (auto num : glb_parts_nchannel) {
        LOG("parts_nch[%d] = %d\n", part_idx, num);
        part_idx++;
    }
    LOG("neighborList:\n");
    for (auto p : neighborList) {
        LOG("%d: %s\n", p.first, p.second.c_str());
    }
    LOG("=========================\n");

    signal(SIGPIPE, SIG_IGN);

    tid = nthreads + 1;

    int main_epfd = epoll_create(MAX_CONNS);
    if (main_epfd < 0) {
        perror("epoll_create failed");
        return -1;
    }
    std::vector<std::thread> threads(nthreads + 1);
    for (int i = 0; i <= nthreads; ++i) {
        int r;
        r = pipe(ctrl_pipe[i]);
        assert(r == 0);
        r = pipe(ctrl_rev_pipe[i]);
        assert(r == 0);
        struct epoll_event ev = (struct epoll_event) {
            .events = EPOLLIN,
            .data = (union epoll_data) {
                .fd = ctrl_rev_pipe[i][0]
            }
        };
        epoll_ctl(main_epfd, EPOLL_CTL_ADD, ctrl_rev_pipe[i][0], &ev);
        if (i < nthreads) {
            threads[i] = std::thread(worker_main, i);
        } else {
            threads[i] = std::thread(acceptor_main, i, msg_manager_socket);
        }
    }
    for (int i = 0; i < nhosts; i++) {
        if (i == host_idx) {
            continue;
        }
        struct epoll_event ev = (struct epoll_event) {
            .events = EPOLLIN,
            .data = (union epoll_data) {
                .fd = remote_ctrl_rev_pipe[i][0]
            }
        };
        epoll_ctl(main_epfd, EPOLL_CTL_ADD, remote_ctrl_rev_pipe[i][0], &ev);
    }

    // start the first iteration
    long start_ts = gettime_ns();
    std::cout << std::format("{:.6f}: STAGE_BUILDUP @ part {}", gettime_ns() / 1e9, iteration_idx) << std::endl;
    for (auto u : glb_all_parts[0]) {
        glb_seen_nodes.insert(u);
    }
    for (auto u : glb_all_cut) {
        glb_seen_nodes.insert(u);
    }
    start_nodes(image, glb_local_parts[0], neighborList, neighborList.size(), logPath);
    start_nodes(image, glb_local_cut, neighborList, neighborList.size(), logPath);
    std::cout << std::format("{:.6f}: start_nodes done", gettime_ns() / 1e9) << std::endl;

    int nev;
    while ((nev = epoll_wait(main_epfd, events, MAX_CONNS, timeout)) >= 0) {
        if (gettime_ns() - start_ts > max_runtime_ns) {
            std::cout << std::format("{:.6f}: Max runtime reached, exiting...", gettime_ns() / 1e9) << std::endl;
            stage = STAGE_TEARDOWN;
            break;
        }
        LOG("epoll_wait() = %d\n", nev);
        for (int i = 0; i < nev; ++i) {
            int event = events[i].events;
            assert(event == EPOLLIN);
            int fd = events[i].data.fd;
            // fd is ctrl_rev_pipe or remote_ctrl_rev_pipe
            // currently only for last_msg_ts update, just read cmd and ignore
            read_int(fd);
        }
        stage_transition(nev);
        if (stage == STAGE_END) {
            break;
        }
    }

    for (int i = 0; i <= nthreads; ++i) {
        {
            std::unique_lock lock(worker_ctrl_pipe_mutex[i]);
            write_int(ctrl_pipe[i][1], 2);
        }
        threads[i].join();
    }

    g_replay_mnger.export_iolog();
    return 0;
}
