#!/usr/bin/env python3
import sys
import re


def parse_perf_output(file_path, start_time, end_time):
    # Initialize counters
    cpu_migrations_count = 0
    context_switches_count = 0

    # Convert time range to float
    start_time = float(start_time)
    end_time = float(end_time)

    # Open file and parse line by line
    with open(file_path, "r") as file:
        for line in file:
            # Match timestamp and event type in line
            match = re.search(
                r"\s+(\d+\.\d+):\s+(\d+)\s+(cpu-migrations|context-switches):", line
            )
            if match:
                # Extract timestamp, event count and event type
                timestamp = float(match.group(1))
                event_count = int(match.group(2))
                event_type = match.group(3)

                # Check whether timestamp is in range
                if start_time <= timestamp <= end_time:
                    if event_type == "cpu-migrations":
                        cpu_migrations_count += event_count
                    elif event_type == "context-switches":
                        context_switches_count += event_count

    return cpu_migrations_count, context_switches_count


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
    cpu_migrations, context_switches = parse_perf_output(
        perf_file, start_time, end_time
    )

    # Print statistics results
    print(f"CPU Migrations: {cpu_migrations}")
    print(f"Context Switches: {context_switches}")
