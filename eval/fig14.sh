#!/bin/bash
source .venv/bin/activate

mode="brief"
if [ "$#" -eq 1 ] && [ "$1" = "complete" ]; then
    mode="complete"
fi

# run frr FT30 with different number of parts
./run.sh eval/config/$mode/fig14/

# plot fig14
./eval/collect_csv.py results/ -o eval/data/real.csv
./eval/plot.py --plot fig14
