#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <unordered_set>
#include <set>
#include <format>
#include "json.hpp"
#include "node_ops.hpp"
#include "debug.hpp"
#include "const.hpp"

extern long gettime_ns(int clock_id = CLOCK_MONOTONIC);

struct ExecResult {
    // 2 parts:
    // - normally exited or not (WIFEXITED(status))
    // - if normally exited then the exit code (WEXITSTATUS(status))
    //    - this field may have other meanings if not normally exited
    int exit_status;
    std::string std_output;
};

static struct ExecResult execInst(const std::string& cmd_in, const std::string& logPath, bool check=true, int timeout=-1) {
    ExecResult result;
    long time_ns = gettime_ns();
    double ts = (double)time_ns / 1e9;
    std::string cmd = "chrt -o 0 " + (timeout > 0 ? "timeout -k 1 " + std::to_string(timeout) + " " : "") +  cmd_in;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        std::ofstream logFile(logPath + "/switch_pods.log", std::ios::app);
        logFile << std::format("{:.6f}: Failed to run command `{}`: {}\n", ts, cmd, strerror(errno));
        logFile.close();
        throw std::runtime_error("Failed to run command: " + cmd);
    }

    std::ostringstream std_output;
    thread_local char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std_output << buffer;
    }
    result.std_output = std_output.str();

    int exit_status = pclose(pipe);
    int exit_code = WEXITSTATUS(exit_status);
    result.exit_status = exit_status;

    if (!WIFEXITED(exit_status) || exit_code != 0) {
        std::ofstream logFile(logPath + "/switch_pods.log", std::ios::app);
        logFile << std::format("{:.6f}: {}\n", ts, cmd);
        logFile << std::format("Return code: {}\n", exit_code);
        logFile << std::format("Stdout:\n{}\n", result.std_output);
        logFile.close();
        if (check) {
            throw std::runtime_error("Command failed: " + cmd + "\nstdout: " + result.std_output);
        }
    }

    std::ofstream logFile(logPath + "/switch_pods.log", std::ios::app);
    logFile << std::format("{:.6f}: {}\n", ts, cmd);
    logFile.close();

    return result;
}

static struct ExecResult retry_until(
    const std::string& cmd_in,
    const std::string& logPath,
    std::function<bool(ExecResult &)> success,
    std::function<void(ExecResult &)> onfail
) {
    do {
        auto result = execInst(cmd_in, logPath, false);
        if (success(result)) {
            return result;
        } else {
            onfail(result);
        }
    } while (true);
}

std::vector<std::string> stop_daemons_cmds_frr(const std::string &node_name) {
    return {
        "./lwc/target/release/lwc exec " + node_name + " /usr/lib/frr/frrinit.sh stop"
    };
}

std::vector<std::string> stop_daemons_cmds_bird(const std::string &node_name) {
    return {
        "./lwc/target/release/lwc exec " + node_name + " bash -c 'pgrep bird | xargs kill'"
    };
}

std::vector<std::string> stop_daemons_cmds_crpd(const std::string &node_name) {
    return {
        "./lwc/target/release/lwc exec " + node_name + " sv force-stop /etc/service/rpd",
        "./lwc/target/release/lwc exec " + node_name + " bash -c 'pgrep -x rpd | xargs kill -9 &> /dev/null'",
    };
}

void stop_nodes(const std::string& image, const std::unordered_set<int>& nodes, const std::string& logPath) {
    std::vector<std::thread> threads;

    for (int node : nodes) {
        threads.emplace_back([&, node]() {
            std::vector<std::string> cmds;

            if (image == "frr") {
                cmds = stop_daemons_cmds_frr("emu-real-" + std::to_string(node));
            } else if (image == "bird") {
                cmds = stop_daemons_cmds_bird("emu-real-" + std::to_string(node));
            } else if (image == "crpd") {
                cmds = stop_daemons_cmds_crpd("emu-real-" + std::to_string(node));
            } else {
                dbg_assert(0, "Unimplemented stop_nodes() for image %s\n", image.c_str());
            }

            for (const auto& cmd : cmds) {
                execInst(cmd, logPath, false, EXEC_TIMEOUT_IN_SEC);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

std::vector<std::string> start_daemons_cmds_frr(const std::string &node_name) {
    return {
        "./lwc/target/release/lwc cp ./daemons " + node_name + ":/etc/frr/daemons",
        "mkdir -p /opt/lwc/volumes/ripc/" + node_name + "/",
        "chmod a+rwx -R /opt/lwc/volumes/ripc/" + node_name + "/",
        /** NOTE: the -d flag is required */
        // TODO: find out why. BGP connect() would block is NOT the reason.
        "./lwc/target/release/lwc exec -d " + node_name +
            // " strace -f -o /var/log/real/strace.log bash -c '/usr/lib/frr/frrinit.sh start &> /var/log/real/frrinit.log'"
            " bash -c '/usr/lib/frr/frrinit.sh start &> /var/log/real/frrinit.log'"
    };
}

std::vector<std::string> start_daemons_cmds_bird(const std::string &node_name) {
    return {
        "mkdir -p /opt/lwc/volumes/ripc/" + node_name + "/",
        "chmod a+rwx -R /opt/lwc/volumes/ripc/" + node_name + "/",
        "./lwc/target/release/lwc exec " + node_name +
            " mkdir -p /var/log/real/",
        "./lwc/target/release/lwc exec " + node_name +
            " bird -D /var/log/real/bird.log"
            // " strace -ttt -ff -o /var/log/real/strace.log bird -D /var/log/real/bird.log'"
    };
}

void start_daemons_crpd(const std::string &node_name, const std::string &logPath) {
    execInst("./lwc/target/release/lwc start " + node_name + " /sbin/runit-init.sh", logPath);
    execInst("./lwc/target/release/lwc exec " + node_name + " mkdir -p /var/license/", logPath);
    execInst("./lwc/target/release/lwc cp ./crpd-license " + node_name + ":/var/license/crpd-license", logPath);
    auto retry_timer = [] (ExecResult & res) { nanosleep((const struct timespec[]){{0, 2500000000 /*2500ms*/}}, NULL); };
    retry_until("./lwc/target/release/lwc exec " + node_name + " bash -c \"cli -c 'request system license add /var/license/crpd-license' 2> /dev/null\"", logPath,
        [] (ExecResult & res) { return res.std_output.find("add license complete") != std::string::npos; },
        retry_timer
    );
    retry_until("./lwc/target/release/lwc exec " + node_name + " bash -c 'cli << EOF\nconfig\nload override /etc/crpd/crpd.conf\ncommit\nEOF\n'", logPath,
        [] (ExecResult & res) { return WIFEXITED(res.exit_status) && WEXITSTATUS(res.exit_status) == 0; },
        retry_timer
    );
}

void restart_daemons_crpd(const std::string &node_name, const std::string &logPath) {
    execInst("./lwc/target/release/lwc exec " + node_name + " sv force-restart rpd", logPath);
}

void start_nodes(const std::string& image,
                   const std::unordered_set<int>& nodes,
                   const std::unordered_map<int, std::string>& neighborList,
                   int nNodes,
                   const std::string& logPath) {
    std::vector<std::thread> threads;
    for (int node : nodes) {
        threads.emplace_back([&, node]() {
            std::string node_name = "emu-real-" + std::to_string(node);
            if (image == "crpd") {
                start_daemons_crpd(node_name, logPath);
                return;
            }
            std::vector<std::string> cmds;
            if (image == "frr") {
                cmds = start_daemons_cmds_frr(node_name);
            } else if (image == "bird") {
                cmds = start_daemons_cmds_bird(node_name);
            } else {
                dbg_assert(0, "Unimplemented start_nodes() for image %s\n", image.c_str());
            }

            for (const auto& cmd : cmds) {
                execInst(cmd, logPath);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

void restart_nodes(const std::string& image,
                   const std::unordered_set<int>& nodes,
                   const std::unordered_map<int, std::string>& neighborList,
                   int nNodes,
                   const std::string& logPath) {
    std::vector<std::thread> threads;
    for (int node : nodes) {
        threads.emplace_back([&, node]() {
            std::string node_name = "emu-real-" + std::to_string(node);
            if (image == "crpd") {
                restart_daemons_crpd(node_name, logPath);
                return;
            }
            std::vector<std::string> cmds;
            if (image == "frr") {
                cmds = start_daemons_cmds_frr(node_name);
            } else if (image == "bird") {
                cmds = start_daemons_cmds_bird(node_name);
            } else {
                dbg_assert(0, "Unimplemented restart_nodes() for image %s\n", image.c_str());
            }

            for (const auto& cmd : cmds) {
                execInst(cmd, logPath);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}

std::vector<std::string> export_routes_cmds_frr(const std::string &node_name, const std::string &tag)
{
    return {
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c \"vtysh -c 'show ip bgp summary' &> /var/log/real/bgp_summary-" + tag + ".log\"",
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c \"vtysh -c 'show bgp all' &> /var/log/real/bgp_routes-" + tag + ".log\""
    };
}

std::vector<std::string> export_routes_cmds_bird(const std::string &node_name, const std::string &tag)
{
    return {
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c 'birdc show protocols &> /var/log/real/bgp_summary-" + tag + ".log'",
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c 'birdc show route all &> /var/log/real/bgp_routes-" + tag + ".log'"
    };
}

std::vector<std::string> export_routes_cmds_crpd(const std::string &node_name, const std::string &tag)
{
    return {
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c 'cli -c \"show route\" &> /var/log/real/bgp_routes-" + tag + ".log'",
        "./lwc/target/release/lwc exec " + node_name +
            " bash -c 'cli -c \"show bgp summary\" &> /var/log/real/bgp_summary-" + tag + ".log'"
    };
}

void export_routes(const std::string& image, const std::unordered_set<int>& nodes, const std::string& tag, const std::string& logPath) {
    std::vector<std::thread> threads;

    std::set<int> sorted_nodes;
    for (int node : nodes) {
        sorted_nodes.insert(node);
    }

    int idx = -1;
    int n_nodes = sorted_nodes.size();
    for (int node : sorted_nodes) {
        idx++;
        if (idx >= 2 && idx + 2 < n_nodes) {
            continue;
        }
        threads.emplace_back([&, node]() {
            std::vector<std::string> cmds;
            if (image == "frr") {
                cmds = export_routes_cmds_frr("emu-real-" + std::to_string(node), tag);
            } else if (image == "bird") {
                cmds = export_routes_cmds_bird("emu-real-" + std::to_string(node), tag);
            } else if (image == "crpd") {
                cmds = export_routes_cmds_crpd("emu-real-" + std::to_string(node), tag);
            } else {
                dbg_assert(0, "Unimplemented export_routes() for image %s\n", image.c_str());
            }

            for (const auto& cmd : cmds) {
                execInst(cmd, logPath, false, EXEC_TIMEOUT_IN_SEC);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}
