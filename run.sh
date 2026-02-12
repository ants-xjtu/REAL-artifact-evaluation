#!/bin/bash

LOCKFILE="/var/lock/real.lock"

exec 9>"$LOCKFILE" || exit 1
if ! flock -n 9; then
  echo "Another emulation is already running. Please wait."
  exit 1
fi

sudo ./scripts/utils/cleanup.sh &> cleanup.log
sudo bash -c 'source .venv/bin/activate; exec ./scripts/runtime/run.py "$@"' bash "$@"
