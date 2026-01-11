#!/bin/bash
source .venv/bin/activate

mode="brief"
if [ "$#" -eq 1 ] && [ "$1" = "complete" ]; then
    mode="complete"
fi

./run.sh eval/config/$mode
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
