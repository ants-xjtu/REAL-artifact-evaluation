#!/bin/bash
source .venv/bin/activate

# run preload cases: this is slow
./run.sh eval/preload/bird.yaml
./run.sh eval/preload/crpd.yaml
./run.sh eval/preload/crpd_iter.yaml
./run.sh eval/preload/frr.yaml
./run.sh eval/preload/frr_iter
./run.sh eval/preload/bird_iter
# run baseline cases: this is super slow
./run.sh eval/baseline/
# run batfish cases
cd batfish
./run.sh 64 fattree 20
./run.sh 64 fattree 24
./run.sh 64 fattree 28
./run.sh 64 topozoo Kdl
./run.sh 64 dupzoo Kdl:2
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
