#pragma once

#include "message.hpp"
#include "remote_channel.hpp"
#include "const.hpp"

#include <thread>
#include <vector>
#include <unordered_map>
#include <condition_variable>

struct EpollThread {
    int epfd;
    int ctrl_rev_fd;
    std::vector<int> host_ids;
    std::unordered_map<int, RemoteChannel *> fd2hostch;
};

struct ConnectionManager {
    int self_id;
    int listen_fd;
    std::atomic<int> connected_count{0};
    int total_expected = 0; // nhosts - 1

    std::mutex mtx;
    std::condition_variable cv;

    void notify_connected() {
        int curr = connected_count.fetch_add(1) + 1;
        if (curr == total_expected) {
            cv.notify_all();
        }
    }

    void wait_all_connected() {
        std::unique_lock lk(mtx);
        cv.wait(lk, [&]() { return connected_count.load() == total_expected; });
    }
};

struct Host {
    int id;
    std::string ip;
    int port;
};

std::pair<int, int> inter_host_buildup(std::string hosts_file);
