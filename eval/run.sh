#!/bin/bash

mkdir eval/data eval/figures
./eval/fig2_3.sh "$@"
./eval/fig14.sh "$@"
./eval/fig1_9_10.sh "$@"
./eval/fig11_12.sh "$@"
./eval/fig13.sh "$@"

# TODO: fig15
