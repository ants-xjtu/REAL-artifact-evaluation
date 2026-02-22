#include "channel_manager.hpp"
#include <fstream>
#include <format>

extern std::string logPath;

ChannelManager g_channel_manager;

void ChannelManager::export_latency_csv() {
    std::ofstream file(logPath + "/channel_latency.csv");
    file << "timestamp,conf\n";
    for (const auto& [key, ts] : completion_ts_) {
        long latency_ns = ts - baseline_ts_;
#ifdef TWO_PHASE_DISABLED
        file << std::format("{:.6f},no-two-phase\n", latency_ns / 1e9);
#else
        file << std::format("{:.6f},two-phase\n", latency_ns / 1e9);
#endif
    }
}
