#!/usr/bin/env python3

import re
import sys


def parse_ns2pidlist(perf_file_path):
    ns2pidlist = {}
    pid2ns = {}
    current_pid = None

    with open(perf_file_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m1 = re.search(r"PERF_RECORD_NAMESPACES (\d+)/(\d+)", line)
            if m1:
                current_pid = m1.group(2)
                continue
            m2 = re.search(r"3/pid: (\d+/\S+),", line)
            if m2 and current_pid:
                pid_nsid = m2.group(1)
                ns2pidlist.setdefault(pid_nsid, []).append(current_pid)
                pid2ns[current_pid] = pid_nsid
    return ns2pidlist, pid2ns


def parse_dockername_pidlist(dockername_pidlist_filename):
    dockername_pidlist = {}
    with open(dockername_pidlist_filename, "r") as f:
        for line in f:
            parts = line.strip().split()
            if not parts:
                continue
            dockername = parts[0]
            pids = parts[1:]
            dockername_pidlist[dockername] = pids
    return dockername_pidlist


def map_pid_to_dockername(dockername_pidlist, ns2pidlist, pid2ns):
    pid2dockername = {}
    for dockername, docker_pids in dockername_pidlist.items():
        for pid in docker_pids:
            nsid = pid2ns.get(pid)
            if nsid and nsid in ns2pidlist:
                for ns_pid in ns2pidlist[nsid]:
                    pid2dockername[ns_pid] = dockername
    return pid2dockername


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(
            "Usage: ./perf_ns_to_dockername.py <cycles.perf> <docker_to_pidalive>",
            file=sys.stderr,
        )
        sys.exit(1)

    perf_file = sys.argv[1]
    dockername_pid_file = sys.argv[2]

    ns2pidlist, pid2ns = parse_ns2pidlist(perf_file)
    dockername_pidlist = parse_dockername_pidlist(dockername_pid_file)
    pid2dockername = map_pid_to_dockername(dockername_pidlist, ns2pidlist, pid2ns)

    for pid, dockername in pid2dockername.items():
        print(f"{pid} {dockername}")
