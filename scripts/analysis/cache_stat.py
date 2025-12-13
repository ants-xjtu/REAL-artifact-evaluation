#!/usr/bin/env python3
import sys
import re


def parse_perf_output(file_path, start_time, end_time):
    # Initialize counters
    counters = {
        "l1d-load": 0,
        "l1d-miss": 0,
        "l2-miss": 0,
        "llc-miss": 0,
        "dtlb-load": 0,
        "dtlb-miss": 0,
        "itlb-miss": 0,
        "instructions": 0,
    }

    # Convert time range to float
    start_time = float(start_time)
    end_time = float(end_time)

    # Open file and parse line by line
    with open(file_path, "r") as file:
        for line in file:
            # Match timestamp and event type in line
            match = re.search(
                r"\s+(\d+\.\d+):\s+(\d+)\s+(L1-dcache-load-misses|L1-dcache-loads|LLC-loads|LLC-load-misses|dTLB-load-misses|dTLB-loads|iTLB-load-misses|instructions):",
                line,
            )
            if match:
                # Extract timestamp, event count and event type
                timestamp = float(match.group(1))
                event_count = int(match.group(2))
                event_type = match.group(3)

                # Check whether timestamp is in range
                if start_time <= timestamp <= end_time:
                    match event_type:
                        case "L1-dcache-load-misses":
                            counters["l1d-miss"] += event_count
                        case "L1-dcache-loads":
                            counters["l1d-load"] += event_count
                        case "LLC-loads":
                            counters["l2-miss"] += event_count
                        case "LLC-load-misses":
                            counters["llc-miss"] += event_count
                        case "dTLB-load-misses":
                            counters["dtlb-miss"] += event_count
                        case "dTLB-loads":
                            counters["dtlb-load"] += event_count
                        case "iTLB-load-misses":
                            counters["itlb-miss"] += event_count
                        case "instructions":
                            counters["instructions"] += event_count

    return counters


def parse_time_ranges(range_file):
    time_ranges = {}
    with open(range_file, "r") as file:
        for line in file:
            parts = line.strip().split()
            if len(parts) == 2:
                time_ranges[parts[0]] = float(parts[1])
    return time_ranges


if __name__ == "__main__":
    # Check command line arguments
    if len(sys.argv) != 5:
        print(
            "Usage: python script.py <perf_file> <range_file> <start_event> <end_event>"
        )
        sys.exit(1)

    # Get command line arguments
    perf_file = sys.argv[1]
    range_file = sys.argv[2]
    start_event = sys.argv[3]
    end_event = sys.argv[4]

    # Parse time range file
    time_ranges = parse_time_ranges(range_file)

    if start_event not in time_ranges or end_event not in time_ranges:
        print(f"Error: Events {start_event} or {end_event} not found in range file.")
        sys.exit(1)

    # Get time range
    start_time = time_ranges[start_event]
    end_time = time_ranges[end_event]

    # Call parser function and obtain result
    counters = parse_perf_output(perf_file, start_time, end_time)

    # Print statistics results
    for k, v in counters.items():
        print(f"{k}: {v}")
