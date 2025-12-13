#!/bin/bash

# Get current system boot time (as UNIX timestamp)
BOOT_TIME=$(awk '{printf "%.0f\n", systime() - $1}' /proc/uptime | bc)

echo $BOOT_TIME

# Get monotonic time (in seconds)
MONOTONIC_SECONDS=$1

if [[ -z "$MONOTONIC_SECONDS" ]]; then
  echo "Usage: $0 <monotonic_time_in_seconds>"
  exit 1
fi

# Add monotonic time to boot time to obtain UNIX timestamp
UNIX_TIME=$(echo "$BOOT_TIME + $MONOTONIC_SECONDS" | bc)

# Convert UNIX timestamp to date format (YYYY-MM-DD)
DATE=$(date -d "@$UNIX_TIME" "+%Y-%m-%d %H:%M:%S")

echo "Monotonic time: $MONOTONIC_SECONDS seconds"
echo "Translated to: $DATE"
