#!/usr/bin/env python3

import sys
import re
from collections import defaultdict


def parse_perf_script(filename, tolerance=0.1):
    cpu_data = defaultdict(list)

    with open(filename, "r") as f:
        for line in f:
            timestamp_match = re.search(
                r"\[(\d+)\]\s+(\d+\.\d+):\s+(\d+)\s+cpu-clock:", line
            )
            if timestamp_match:
                cpu_id = int(timestamp_match.group(1))
                timestamp = float(timestamp_match.group(2))
                cpu_clock = int(timestamp_match.group(3))
                cpu_data[cpu_id].append((timestamp, cpu_clock))

    for cpu_id, samples in cpu_data.items():
        if len(samples) < 2:
            print(f"Insufficient data for CPU {cpu_id} for analysis.")
            continue

        timestamps = [sample[0] for sample in samples]
        cpu_clocks = [sample[1] for sample in samples]

        print(f"CPU {cpu_id}:")

        for i in range(1, len(timestamps)):
            expected_interval = cpu_clocks[i]
            interval_tolerance = expected_interval * tolerance

            actual_interval = (timestamps[i] - timestamps[i - 1]) * 1e9
            interval_difference = abs(actual_interval - expected_interval)

            if interval_difference > interval_tolerance:
                print(
                    f"  Significant sample loss detected between index {i-1} and {i}:"
                )
                print(f"    Previous timestamp: {timestamps[i - 1]} s")
                print(f"    Current timestamp: {timestamps[i]} s")
                print(f"    Actual interval: {actual_interval} ns")
                print(f"    Expected interval (cpu-clock): {expected_interval} ns")
                print(
                    f"    Interval difference: {interval_difference} ns ({interval_difference / expected_interval * 100}%)"
                )
                print("    -----------------------------")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python check_sample_loss.py <perf-script-output-file>")
        sys.exit(1)

    perf_script_output = sys.argv[1]
    parse_perf_script(perf_script_output)
