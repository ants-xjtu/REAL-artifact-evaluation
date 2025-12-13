#!/usr/bin/env python3

import sys
import re


def parse_logs(log_file, perf_file, dst_dir, prefix):
    stages = []
    current_stage = None

    stage_timestamps = {}
    perf_file_handles = {}

    try:
        log_f = open(log_file, "r")
        perf_f = open(perf_file, "r")

        for line in log_f:
            if not line.strip():
                continue
            if not re.match(r"^\d+\.\d+:", line.strip()):
                match = re.search(r"(\b\w+\b) (\d+\.\d+)", line)
                if not match:
                    print(line)
                    assert match
                stage = match.group(1)
                timestamp = match.group(2)
                if current_stage is None or current_stage != stage:
                    current_stage = stage
                    stages.append(current_stage)
                    stage_timestamps[current_stage] = float(timestamp)
                    perf_file_handles[current_stage] = open(
                        f"{dst_dir}/{prefix}{current_stage}.perf", "a"
                    )

        stage_index = 0
        for line in perf_f:
            match = re.match(r"^\w+\s+\d+\s+\[\d+\]\s+(\d+\.\d+):", line)
            if match:
                perf_timestamp = float(match.group(1))
                while (
                    stage_index < len(stage_timestamps) - 1
                    and perf_timestamp >= stage_timestamps[stages[stage_index + 1]]
                ):
                    stage_index += 1

            perf_file_handles[stages[stage_index]].write(line)

    finally:
        log_f.close()
        perf_f.close()

        for handle in perf_file_handles.values():
            handle.close()

    return stages


if __name__ == "__main__":
    if len(sys.argv) != 4 and len(sys.argv) != 5:
        print("Usage: python script.py <log_file> <perf_file> <dst_dir> [prefix]")
        sys.exit(1)

    log_file = sys.argv[1]
    perf_file = sys.argv[2]
    dst_dir = sys.argv[3]
    prefix = "" if len(sys.argv) == 4 else sys.argv[4]

    stages = parse_logs(log_file, perf_file, dst_dir, prefix)
    for stage in stages:
        print(stage)
