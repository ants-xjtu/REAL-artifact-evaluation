# REAL NSDI'26 Artifact Evaluation

This repository provides the artifacts and instructions to conduct artifact evaluation for the NSDI'26 paper "REAL: Emulating Control Plane at Simulator’s Cost".

## Getting Started Instructions

We provide a remote execution environment for the Artifact Evaluation Committee to reproduce the main results, see [remote environment access guide](./REMOTE_ACCESS.md). If you are interested in reproduce the results on your own machine, see [environment setup guide](./ENV_SETUP.md) to setup your own environment.

After gaining access to the environment, try the following commands:

```bash
./run.sh test/basic_coverage/baseline/frr.yaml
./run.sh test/basic_coverage/preload/frr.yaml
```

You should see something like this in the output:

```txt
$ ./run.sh test/basic_coverage/baseline/frr.yaml
----------------
Running: ./scripts/config/confgen.py frr fattree 2
conf/frr/fattree2
Running: ./scripts/runtime/run_one.sh -i frr -T fattree2 -m baseline -t test-frr-baseline -w 30 -x 2025-12-23_12-40-04 -C 0-63
./results/baseline_frr_fattree2_test-frr-baseline_0-63_2025-12-23_12-40-04 ./pics/baseline_frr_fattree2_test-frr-baseline_0-63_2025-12-23_12-40-04
Verifying: ./scripts/test/check_bgp_summary.py -d ./results/baseline_frr_fattree2_test-frr-baseline_0-63_2025-12-23_12-40-04 -i frr -t fattree2
Verifying: ./scripts/test/check_bgp_routes.py -d ./results/baseline_frr_fattree2_test-frr-baseline_0-63_2025-12-23_12-40-04 -i frr -t fattree2
Verifying: ./scripts/test/check_charts.sh ./pics/baseline_frr_fattree2_test-frr-baseline_0-63_2025-12-23_12-40-04 baseline false
[Passed]
All runs processed successfully.
$ ./run.sh test/basic_coverage/preload/frr.yaml
----------------
Running: ./scripts/config/confgen.py frr fattree 2
conf/frr/fattree2
Running: ./scripts/runtime/run_one.sh -i frr -T fattree2 -m preload -t test-frr-preload -w 30 -x 2025-12-23_12-41-03 -C 0-63
./results/preload_frr_fattree2_test-frr-preload_0-63_2025-12-23_12-41-03 ./pics/preload_frr_fattree2_test-frr-preload_0-63_2025-12-23_12-41-03
Verifying: ./scripts/test/check_bgp_summary.py -d ./results/preload_frr_fattree2_test-frr-preload_0-63_2025-12-23_12-41-03 -i frr -t fattree2
Verifying: ./scripts/test/check_bgp_routes.py -d ./results/preload_frr_fattree2_test-frr-preload_0-63_2025-12-23_12-41-03 -i frr -t fattree2
Verifying: ./scripts/test/check_charts.sh ./pics/preload_frr_fattree2_test-frr-preload_0-63_2025-12-23_12-41-03 preload false
[Passed]
All runs processed successfully.
```

## Detailed Instructions

See [the evaluation guide](./EVALUATION_GUIDE.md) for the artifact claims and the instructions to reproduce the main results of the paper.

## Citation

If you find the code useful, please consider citing our paper.

```bibtex
@inproceedings {316726,
author = {Ze Xia and Hao Li and Jinyu Fu and Xin Wan and Yihan Dang and Danfeng Shan and Li Chen and Peng Zhang},
title = {{REAL}: Emulating Control Plane at {Simulator{\textquoteright}s} Cost},
booktitle = {23rd USENIX Symposium on Networked Systems Design and Implementation (NSDI 26)},
year = {2026},
isbn = {978-1-939133-54-0},
address = {Renton, WA},
pages = {1861--1877},
url = {https://www.usenix.org/conference/nsdi26/presentation/xia},
publisher = {USENIX Association},
month = may
}
```
