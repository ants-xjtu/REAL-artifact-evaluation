#!/bin/bash

LOCKFILE="/var/lock/real-ae.lock"

exec 9>"$LOCKFILE" || exit 1
if ! flock -n 9; then
  echo "Another evaluation is already running. Please wait."
  exit 1
fi

source .venv/bin/activate

# Define the tag for this experiment (matches r2i-*.yaml configs)
TAG="ae-r2i"

# Run the experiments
./run.sh eval/config/r2i-improved.yaml
./run.sh eval/config/r2i-baseline.yaml

# Find result directories by tag (use latest if multiple)
RESULT_DIR_IMPROVED=$(ls -td results/preload_frr_*_${TAG}-improved_* 2>/dev/null | head -1)
RESULT_DIR_BASELINE=$(ls -td results/preload_frr_*_${TAG}-baseline_* 2>/dev/null | head -1)

if [ -z "$RESULT_DIR_IMPROVED" ] || [ -z "$RESULT_DIR_BASELINE" ]; then
    echo "No result directories found with tag: ${TAG}-improved or ${TAG}-baseline"
    exit 1
fi

echo "Using result directories:"
echo "  Improved: $RESULT_DIR_IMPROVED"
echo "  Baseline: $RESULT_DIR_BASELINE"

mkdir -p eval/data eval/figures

# --- FIG11: CDF Data (baseline vs improved) ---
echo "timestamp,conf" > eval/data/cdf.csv
if [ -f "$RESULT_DIR_IMPROVED/channel_latency.csv" ]; then
    grep -v "^timestamp" "$RESULT_DIR_IMPROVED/channel_latency.csv" >> eval/data/cdf.csv
fi
if [ -f "$RESULT_DIR_BASELINE/channel_latency.csv" ]; then
    grep -v "^timestamp" "$RESULT_DIR_BASELINE/channel_latency.csv" >> eval/data/cdf.csv
fi
echo "Fig11 CDF data written to eval/data/cdf.csv"

# --- FIG12: Event Chart Data (improved only) ---
cp "$RESULT_DIR_IMPROVED/converge.perf" eval/data/r2i_converge.perf
cp "$RESULT_DIR_IMPROVED/pid_to_dockername" eval/data/r2i_pid_to_dockername
cp "$RESULT_DIR_IMPROVED/stage_border_ts" eval/data/r2i_stage_border_ts
echo "Fig12 data files copied"

# --- Generate plots ---
./eval/plot.py --plot fig11
./eval/plot.py --plot fig12
