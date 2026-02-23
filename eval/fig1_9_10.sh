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

./run.sh eval/config/$mode/fig9
cd batfish
./run.sh 64 fattree 24
./run.sh 64 fattree 28
./run.sh 64 topozoo Kdl
cd ..

./eval/collect_csv.py results/ -o eval/data/real.csv
./eval/plot.py --plot fig9a
./eval/plot.py --plot fig9b
./eval/plot.py --plot fig9c
./eval/plot.py --plot fig9d
./eval/plot.py --plot fig9e
./eval/plot.py --plot fig9f
./eval/plot.py --plot fig1
./eval/plot.py --plot fig10
