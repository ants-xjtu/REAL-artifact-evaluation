#!/bin/bash

LOCKFILE="/var/lock/real-ae.lock"

exec 9>"$LOCKFILE" || exit 1
if ! flock -n 9; then
  echo "Another evaluation is already running. Please wait."
  exit 1
fi

source .venv/bin/activate

mode="brief"
if [ "$#" -eq 1 ] && [ "$1" = "complete" ]; then
    mode="complete"
fi

TAG="ae-fig13"

# Run the experiments
./run.sh eval/config/$mode/fig13-improved.yaml
./run.sh eval/config/$mode/fig13-baseline.yaml

mkdir -p eval/data eval/figures

# Collect data and generate two-phase.csv
./eval/collect_two_phase.py results/ -o eval/data/two-phase.csv --tag "$TAG"

# Generate plot
./eval/plot.py --plot fig13
