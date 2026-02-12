#!/bin/bash

LOCKFILE="/var/lock/real-ae.lock"

exec 9>"$LOCKFILE" || exit 1
if ! flock -n 9; then
  echo "Another evaluation is already running. Please wait."
  exit 1
fi

source .venv/bin/activate

# run frr FT30 and collect extra information
./run.sh eval/config/fig2.yaml

# plot fig2
cp results/preload_frr_fattree30_ae-fig2_*/{converge.perf,pid_to_dockername,stage_border_ts} eval/data/
./eval/plot.py --plot fig2

# plot fig3
./scripts/analysis/workset.py --iolog_file results/preload_frr_fattree30_ae-fig2_*/io.log -o eval/data/working_set.csv
./eval/plot.py --plot fig3
