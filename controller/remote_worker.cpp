#include "remote_worker.hpp"

#include "remote_channel.hpp"
#include "const.hpp"
#include "json.hpp"
#include "replay_manager.hpp"

#include <format>
#include <cassert>
#include <memory>
#include <thread>
#include <iostream>
#include <fstream>

extern "C" {
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
}

extern long gettime_ns(int clock_id = CLOCK_MONOTONIC);

using json = nlohmann::json;

extern int remote_ctrl_rev_pipe[MAX_THREADS + 1][2];
extern volatile std::atomic<int> n_ready_host;
extern volatile std::atomic<int> stage;

std::array<std::unique_ptr<RemoteChannel>, MAX_HOSTS> remote_channels;
static std::array<std::unique_ptr<EpollThread>, MAX_HOSTS> poll_threads;

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void register_channel(int fd, int host_id, EpollThread* et) {
    auto ch = std::make_unique<RemoteChannel>(fd, host_id, et->epfd);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (::epoll_ctl(et->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        perror("epoll_ctl add");
        close(fd);
        return;
    }

    et->fd2hostch[fd] = ch.get();
    remote_channels[host_id] = std::move(ch);
    et->host_ids.push_back(host_id);
}

static ConnectionManager conn_mgr;
void connector_thread_func(Host host) {
    int fd = -1;
    while (true) {
        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        set_nonblocking(fd);

        sockaddr_in raddr{};
        raddr.sin_family = AF_INET;
        raddr.sin_port   = htons(host.port);
        inet_pton(AF_INET, host.ip.c_str(), &raddr.sin_addr);

        int r = ::connect(fd, (sockaddr*)&raddr, sizeof(raddr));
        if (r == 0) {
            LOG("Connected immediately to host %d\n", host.id);
            break;
        }
        if (r < 0 && errno != EINPROGRESS) {
            LOG("connect failed to host %d: %s, retry in 1s\n",
                host.id, strerror(errno));
            close(fd);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        int epfd = epoll_create1(0);
        struct epoll_event ev{ .events = EPOLLOUT, .data = {.fd = fd} };
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

        struct epoll_event events[1];
        int n = epoll_wait(epfd, events, 1, 1000); // 1s
        close(epfd);

        if (n == 0) {
            close(fd);
            LOG("connect timeout to host %d, retry\n", host.id);
            continue;
        }

        if (events[0].events & (EPOLLOUT | EPOLLERR)) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err == 0) {
                LOG("Connected to host %d\n", host.id);
                int r = ::write(fd, &conn_mgr.self_id, sizeof(int));
                assert(r == sizeof(int));
                break;
            } else {
                LOG("connect failed to host %d: %s, retry in 1s\n",
                    host.id, strerror(err));
                close(fd);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }
    }
    register_channel(fd, host.id, poll_threads[host.id].get());
    conn_mgr.notify_connected();
    LOG("connector thread for host %d exiting\n", host.id);
}

std::tuple<std::vector<Host>, int> parse_hosts(std::string filename) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        LOG("cannot open hosts.json, treating as non-distributed");
        return {{{0, "0.0.0.0", 12345}}, 0};
    }
    json j;
    f >> j;

    std::vector<Host> hosts;
    for (auto &h : j["hosts"]) {
        hosts.push_back({ h["id"], h["ip"], h["port"] });
    }
    int self_id = j["self_id"];
    return {hosts, self_id};
}

void remote_worker_main(EpollThread* et);

std::pair<int, int> inter_host_buildup(std::string hosts_file) {
    auto [hosts, self_id] = parse_hosts(hosts_file);

    int nhosts = (int)hosts.size();
    conn_mgr.self_id = self_id;
    conn_mgr.total_expected = nhosts - 1;

    std::cout << "self host_id: " << self_id << ", remote hosts cnt: " << conn_mgr.total_expected << std::endl;

    for (int i = 0; i < nhosts; i++) {
        if (i == self_id) {
            continue;
        }
        poll_threads[i] = std::make_unique<EpollThread>();
        poll_threads[i]->epfd = epoll_create1(0);
        int r = pipe(remote_ctrl_rev_pipe[i]);
        assert(r == 0);
        poll_threads[i]->ctrl_rev_fd = remote_ctrl_rev_pipe[i][1];
    }

    // 2. create listening socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    set_nonblocking(listen_fd);
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(hosts[self_id].port);
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    listen(listen_fd, 128);
    conn_mgr.listen_fd = listen_fd;

    // 3. start Acceptor thread
    std::thread acceptor([listen_fd, &hosts, self_id]() {
        while (conn_mgr.connected_count < conn_mgr.total_expected) {
            int cfd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                perror("accept");
                continue;
            }

            int host_id = -1;
            int r = read(cfd, &host_id, sizeof(int));
            assert(r == sizeof(int));

            register_channel(cfd, host_id, poll_threads[host_id].get());

            conn_mgr.notify_connected();
        }
    });
    acceptor.detach();

    // 4. actively connect() to host with smaller id
    for (auto& h : hosts) {
        if (h.id == self_id) continue;
        if (h.id < self_id) {
            std::thread(connector_thread_func, h).detach();
        }
    }

    // 5. wait for all hosts to be connected
    conn_mgr.wait_all_connected();
    printf("All %d connections established\n", conn_mgr.total_expected);

    for (int i = 0; i < nhosts; ++i) {
        if (i == self_id) {
            continue;
        }
        std::thread(remote_worker_main, poll_threads[i].get()).detach();
    }

    return {hosts.size(), self_id};
}

static void write_int(int fd, int num) {
    int r = write(fd, &num, sizeof(num));
    assert(r == sizeof(num));
    return;
}

void remote_worker_main(EpollThread* et)
{
    constexpr int MAX_EVENTS = 32;
    struct epoll_event events[MAX_EVENTS];

    while (true) {
        int n = epoll_wait(et->epfd, events, MAX_EVENTS, 100);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            RemoteChannel* ch = et->fd2hostch[fd];
            if (!ch) continue;
            if (ev & (~(EPOLLIN | EPOLLOUT))) {
                ::close(fd);
                et->fd2hostch.erase(fd);
                if (et->fd2hostch.empty()) {
                    return;
                }
                continue;
            }

            if (ev & EPOLLIN) {
                auto msg_list = ch->pollin(et->ctrl_rev_fd);
                bool notify_main = false;
                for (auto &msg : msg_list) {
                    real_hdr_t hdr;
                    memcpy(&hdr, msg->data(), hdrsiz);
                    switch (hdr.msg_type) {
                    case REAL_ENDOFSTAGE: {
                        std::cout << std::format("{:.6f}: recv_eos {} host {}", gettime_ns() / 1e9, stage_name[stage], ch->host_id()) << std::endl;
                        n_ready_host++;
                        break;
                    }
                    case REAL_KEEPBUSY: {
                        LOG("recv keepbusy\n");
                        notify_main = true;
                        break;
                    }
                    case REAL_PAYLOAD: {
                        real_pld_t pld;
                        memcpy(&pld, (uint8_t *)msg->data(), sizeof(pld));
                        g_replay_mnger.add_msg(msg, pld.src_id, pld.dst_id);
                        notify_main = true;
                        break;
                    }
                    default: {
                        dbg_assert(0, "unexpected msg_type: %s\n", msg_type_name[hdr.msg_type]);
                    }
                    }
                }
                if (notify_main) {
                    write_int(et->ctrl_rev_fd, 2);
                }
            }
            if (ev & EPOLLOUT) {
                ch->pollout();
            }
        }
    }
}
