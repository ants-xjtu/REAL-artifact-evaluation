#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 3 ]; then
    echo "Usage: $0 <directory> <mode: preload|baseline> <profile: true|false>"
    exit 1
fi

DIR=$1
MODE=$2
PROFILE=$3

missing_files=()

check_file() {
    local file="$DIR/$1"
    if [ ! -s "$file" ]; then
        missing_files+=("$file")
    fi
}

# always required
check_file "timebar.png"
check_file "mem.csv"
check_file "mem.png"

# profile mode requires all eventchart and svg files
if [ "$PROFILE" = "true" ]; then
    prefixes=(converge create_containers sleep)
    if [ "$MODE" = "baseline" ]; then
        prefixes+=(create_network)
    fi
    for prefix in "${prefixes[@]}"; do
        check_file "${prefix}_eventchart.png"
        check_file "${prefix}_percore_eventchart.png"
        check_file "${prefix}.svg"
        check_file "${prefix}_rev.svg"
        check_file "${prefix}_withidle.svg"
        check_file "${prefix}_withidle_rev.svg"
    done
fi

if [ ${#missing_files[@]} -ne 0 ]; then
    echo "Error: missing files:"
    for f in "${missing_files[@]}"; do
        echo "  $f"
    done
    exit 1
fi

echo "All required files exist."
