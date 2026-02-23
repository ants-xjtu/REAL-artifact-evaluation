#!/usr/bin/env bash
set -euo pipefail

PID="$1"

# Ensure PID exists at start
if [[ ! -r "/proc/$PID/stat" ]]; then
  exit 1
fi

# Record starttime (field 22) to avoid PID reuse bug
STARTTIME=$(awk '{print $22}' "/proc/$PID/stat")

while true; do
  if [[ ! -r "/proc/$PID/stat" ]]; then
    break
  fi
  CUR=$(awk '{print $22}' "/proc/$PID/stat") || break
  [[ "$CUR" == "$STARTTIME" ]] || break
  sleep 0.1
done