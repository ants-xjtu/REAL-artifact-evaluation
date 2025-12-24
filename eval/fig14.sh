#!/bin/bash
source .venv/bin/activate

# run frr FT30 with different number of parts
./run.sh eval/config/fig14/

# plot fig14
./eval/collect_csv.py results/ -o eval/data/real.csv
./eval/plot.py --plot fig14
