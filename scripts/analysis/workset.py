#!/usr/bin/env python3

import matplotlib.pyplot as plt
import re
import time
import argparse
import csv

pid2gid = {}
max_gid = 0


# Parse perf script output
def parse_perf_script(filename):
    if not filename:
        return []
    cmd_line_pat = (
        r"^(\S.*?)\s+(-?\d+)\/*(\d+)*\s+.*?\s+(\d+\.\d+):\s*(\d+)*\s+(\S+):\s*"
    )
    data = []
    with open(filename, "r") as file:
        for line in file:
            res = re.match(cmd_line_pat, line)
            if res:
                pid = res.group(2)
                timestamp = float(res.group(4))
                if pid in pid2gid:
                    gid = pid2gid[pid]
                    d = {"pid": pid, "timestamp": timestamp, "gid": gid}
                    data.append(d)
    return data


# Parse iolog file
def parse_iolog(filename):
    if not filename:
        return []
    cmd_line_pat = r"^(\d+)\s+(\d+\.\d+)"
    data = []
    with open(filename, "r") as file:
        for line in file:
            res = re.match(cmd_line_pat, line)
            if res:
                gid = int(res.group(1))
                timestamp = float(res.group(2))
                global max_gid
                max_gid = max(max_gid, gid)
                d = {"timestamp": timestamp, "gid": gid}
                data.append(d)
    return data


# Argument parsing
parser = argparse.ArgumentParser(description="Perf/iolog compare plot")
parser.add_argument("--iolog_file", help="Path to iolog_file")
parser.add_argument("--perf_file", help="Path to perf_file")
parser.add_argument("--pid_file", help="Path to pid_file")
parser.add_argument("-o", "--output_file", help="Output file path (optional)")
parser.add_argument(
    "-i", "--interval", nargs="*", help="Workset time window (optional)"
)
parser.add_argument("-t", "--title", help="Title for the plot (optional)")

args = parser.parse_args()

iolog_file = args.iolog_file
perf_file = args.perf_file
pid_file = args.pid_file
output_file = args.output_file
plot_title = args.title
interval_list = (
    [float(i) / 1000 for i in args.interval]
    if args.interval
    else [0.05, 0.1, 0.2, 0.5, 1.0]
)

print(
    f"args: {perf_file}, {iolog_file}, {pid_file}, {output_file}, {plot_title}, {interval_list}"
)

# Parse pid_file if provided
if pid_file:
    with open(pid_file, "r") as file:
        for line in file:
            lis = line.split()
            pid = lis[0]
            gid = int(lis[1].split("-")[-1])
            pid2gid[pid] = gid
            max_gid = max(max_gid, gid)

print(f"parse start {time.monotonic()}")
data_perf = parse_perf_script(perf_file)
data_iolog = parse_iolog(iolog_file)
print(f"parse done {time.monotonic()}")

# Collect timestamps
timestamps = [d["timestamp"] for d in data_perf + data_iolog]
base_timestamp = min(timestamps)
last_timestamp = max(timestamps)

# Prepare CSV output
csv_file = (output_file if output_file else perf_file + ".time.png").replace(
    ".png", ".csv"
)
with open(csv_file, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["interval", "time", "perf_count", "iolog_count"])

    # Plotting
    print(f"plotting {time.monotonic()}")
    for interval in interval_list:
        workset_perf = [
            set() for _ in range(int((last_timestamp - base_timestamp) / interval) + 1)
        ]
        workset_iolog = [
            set() for _ in range(int((last_timestamp - base_timestamp) / interval) + 1)
        ]

        for d in data_perf:
            workset_perf[int((d["timestamp"] - base_timestamp) / interval)].add(
                d["gid"]
            )
        for d in data_iolog:
            workset_iolog[int((d["timestamp"] - base_timestamp) / interval)].add(
                d["gid"]
            )

        idx_list = list(range(len(workset_perf)))
        x_list = [float(idx) * interval for idx in idx_list]
        y1_list = [len(workset_perf[x]) for x in idx_list]
        y2_list = [len(workset_iolog[x]) for x in idx_list]

        # Write to CSV
        for t, y1, y2 in zip(x_list, y1_list, y2_list):
            writer.writerow([interval, t, y1, y2])

        # Plot
        if len(data_perf):
            plt.plot(x_list, y1_list, label=f"{int(interval * 1000)} ms (perf)")
        if len(data_iolog):
            plt.plot(x_list, y2_list, label=f"{int(interval * 1000)} ms (iolog)")

plt.legend()
plt.hlines([max_gid], xmin=0, xmax=last_timestamp - base_timestamp)
plt.title(plot_title if plot_title else "Perf vs Iolog Visualization")

print(f"savefig {time.monotonic()}")
if output_file:
    plt.savefig(output_file, format="png")
else:
    plt.savefig(perf_file + ".time.png", format="png")
print(f"savefig done {time.monotonic()}")
