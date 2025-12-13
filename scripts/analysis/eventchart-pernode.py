#!/usr/bin/env python3

import matplotlib
import matplotlib.pyplot as plt
import sys
import re
import time
import argparse

# Function to parse the perf script output file

pid2gid = {}
pid2comm = {}
max_gid = 0
row_set = set()


def parse_perf_script(filename):
    # bgpd_io 445771 [001] 38336.044080: probe:tcp_sendmsg:
    cmd_line_pat = (
        r"^(\S.*?)\s+(-?\d+)\/*(\d+)*\s+.*?\s+(\d+\.\d+):\s*(\d+)*\s+(\S+):\s*"
    )
    data = []
    with open(filename, "r") as file:
        lines = file.readlines()
        for line in lines:
            res = re.match(cmd_line_pat, line)
            if res:
                command = res.group(1)
                if (len(comm_filter) == 0) or (command not in comm_filter):
                    command = "other"
                    # continue
                pid = res.group(2)
                timestamp = float(res.group(4))
                if pid in pid2gid:
                    gid = pid2gid[pid]
                else:
                    gid = max_gid + 1
                pid2comm[pid] = command
                d = {"pid": pid, "timestamp": timestamp, "command": command, "gid": gid}
                data.append(d)
                row_set.add((command, gid))
    return data


# Check if filename provided as command-line argument
if len(sys.argv) < 2:
    print(
        "Usage: python eventchart.py <perf-script-output-file> [-o OUTPUT_FILE] [-t TITLE]"
    )
    sys.exit(1)

# Check if filename provided as command-line argument
parser = argparse.ArgumentParser(description="Process command line arguments")
parser.add_argument("perf_file", help="Path to perf_file (required)")
parser.add_argument("--pid_file", help="Path to pid_file (optional)")
parser.add_argument(
    "--comm_filter", nargs="*", help="Strings to convert to Python list (optional)"
)
parser.add_argument("-o", "--output_file", help="Output file path (optional)")
parser.add_argument("-t", "--title", help="Title for the plot (optional)")
parser.add_argument(
    "-n",
    "--pid_vline",
    help="pid_to_fs (Namespace-specific vertical lines file, optional)",
)
parser.add_argument(
    "-g",
    "--global_vline",
    help="stage_border_ts (Global vertical lines file, optional)",
)

args = parser.parse_args()

perf_file = args.perf_file
pid_file = args.pid_file
comm_filter = [] if args.comm_filter is None else args.comm_filter
output_file = args.output_file
plot_title = args.title
pid_vline_file = args.pid_vline
glb_vline_file = args.global_vline

pid_vline = {}
glb_vline = []

print(f"args: {perf_file}, {pid_file}, {comm_filter}, {output_file}, {plot_title}")

if pid_file:
    with open(pid_file, "r") as file:
        lines = file.readlines()
        for line in lines:
            lis = line.split()
            pid = lis[0]
            gid = int(lis[1].split("-")[-1])
            pid2gid[pid] = gid
            max_gid = max(max_gid, gid)


# Parse the perf script output file
print(f"parse start {time.monotonic()}")
data = parse_perf_script(perf_file)

print(f"parse done {time.monotonic()}")

# Generate color map
cmap = matplotlib.pyplot.get_cmap("rainbow", len(comm_filter) + 1)
cmdind = {comm_name: i for i, comm_name in list(enumerate(comm_filter))}
cmdind["other"] = len(comm_filter)

# Get colors
colors = {
    comm_name: cmap(i)
    for i, comm_name in list(enumerate(comm_filter)) + [(len(comm_filter), "other")]
}

comm_ind = {comm: i for i, comm in enumerate(sorted(comm_filter))}
comm_ind["other"] = len(comm_filter)


def sort_key(r):
    comm = r[0]
    gid = r[1]
    return (gid, comm_ind[comm])


rows = sorted(list(row_set), key=sort_key, reverse=True)
r_to_row = {r: i for i, r in enumerate(rows)}

timestamps = [d["timestamp"] for d in data]
base_timestamp = min(timestamps)
last_timestamp = max(timestamps)
event_low = -0.4  # All events have the same height
event_high = 0.4  # All events have the same height

# Plotting
print(f"plotting {time.monotonic()}")
fig, ax = plt.subplots(figsize=(12, min(655, max(8, len(rows) * 0.15))))

events = [[] for _ in r_to_row]
for d in data:
    row = r_to_row[(d["command"], d["gid"])]
    events[row].append(d["timestamp"] - base_timestamp)

ax.eventplot(events, orientation="horizontal", linewidth=0.75)
ax.axvline(x=0, color="b", linestyle="--")
ax.axvline(x=last_timestamp - base_timestamp, color="b", linestyle="--")

if pid_vline_file:
    with open(pid_vline_file, "r") as file:
        lines = file.readlines()
        for line in lines:
            lis = line.split()
            pid = lis[0]
            key = (
                pid2comm[pid] if pid in pid2comm else "other",
                pid2gid[pid] if pid in pid2gid else max_gid + 1,
            )
            if key not in r_to_row:
                continue
            row = r_to_row[key]
            if row not in pid_vline:
                pid_vline[row] = []
            pid_vline[row] += [float(i) - base_timestamp for i in lis[1:]]

if glb_vline_file:
    with open(glb_vline_file, "r") as file:
        lines = file.readlines()
        for line in lines:
            glb_vline.append(float(line.split()[1]) - base_timestamp)

gid_labels = []
gid_ticks = []

# ns_yrange = {}

for i in range(len(rows) - 1):
    curr_gid = rows[i][1]
    next_gid = rows[i + 1][1]
    if curr_gid != next_gid:
        ax.axhline(y=i + 0.5, color="g", linestyle="-")
        gid_ticks += [i]
        gid_labels += ["host" if curr_gid > max_gid else f"emu-real-{curr_gid}"]
        # if curr_gid in ns_yrange:
        #     ns_yrange[curr_gid][1] = i + 0.5
        # else:
        #     ns_yrange[curr_gid] = [0.5, i + 0.5]
        # if next_gid in ns_yrange:
        #     ns_yrange[next_gid][0] = i + 0.5
        # else:
        #     ns_yrange[next_gid] = [i + 0.5, 0]

# ns_yrange[1][1] = len(rows) + 0.5
# print(ns_yrange)

for row, xlis in pid_vline.items():
    xlis = list(set(xlis))
    for x in xlis:
        ax.vlines(x=x, ymin=row - 0.5, ymax=row + 0.5, color="red", linestyle="-")

for x in glb_vline:
    ax.axvline(x=x, color="g", linestyle="--")

gid_ticks += [len(rows) - 1]
gid_labels += ["emu-real-1"]

plt.tick_params(
    axis="y", which="both", left=False, right=False, labelleft=False, labelright=False
)

comm_axis = ax.secondary_yaxis(location=0)
comm_axis.set_yticks(range(len(rows)))
comm_axis.set_yticklabels((r[0] for r in rows))

ns_axis = ax.secondary_yaxis(location="right")
ns_axis.set_yticks(gid_ticks)
ns_axis.set_yticklabels(gid_labels)
# ns_axis.set_ylabel('cgroup')

ax.axhline(y=-0.5, color="g", linestyle="-")
ax.axhline(y=len(rows) - 0.5, color="g", linestyle="-")

height = len(rows)
vmargin = min(height * 0.1, 1)
plt.ylim(-0.5 - vmargin, height + vmargin)

width = last_timestamp - base_timestamp
hmargin = width * 0.1
plt.xlim(-hmargin, width + hmargin)
ax.set_xlabel("Time")
ax.set_title(plot_title if plot_title else "Perf Script Output Visualization")

# plt.grid(True)
# plt.tight_layout()
print(f"savefig {time.monotonic()}")
if output_file:
    plt.savefig(output_file, format="png")
else:
    plt.savefig(perf_file + ".time.png", format="png")
print(f"savefig done {time.monotonic()}")
