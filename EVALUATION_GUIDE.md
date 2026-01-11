# Artifact Evaluation Guide

First make sure your environment is working by following the "Getting Started Instructions" in [the readme file](./README.md).

## Before You Begin

1. The experiment may take a long time to complete. We strongly recommend running the experiment inside a `tmux` session to ensure stable and uninterrupted execution.
    - Start a tmux session: `tmux new -s real-ae`
    - Detach and leave the session running in background: `Ctrl-b, then d`
    - Reattach to it: `tmux attach -t real-ae`
2. Due to potential naming conflicts, running two experiments simultaneously is currently not supported. If multiple AEC members are conducting experiments, please make sure to stagger the experiment times accordingly.

## One-Command Reproduction

The following one-line command runs all experiments and draws all figures in the paper. If you're interested in what's happening, or running experiments step-by-step, please read [basic usage](#basic-usage) and `eval/run.sh`.

```bash
sudo ./eval/run.sh
```

This evaluation takes approximately **3 hours** to complete, primarily due to the runtime of the baseline and the iterative convergence experiments. It has been deliberately reduced to the minimal experiment set necessary, while still capturing the key performance trends and the operational limits of the system.

> By default, the script executes a selected subset of experiments to reduce execution time. To run all experiments, use `sudo ./eval/run.sh complete`. Note that this full run is very time-consuming (12 hours or more), mainly due to baseline and iterative convergence experiments. We recommend running the brief version only. Also, we recommend reserving no less than 200 GB of free disk space for runtime intermediates and experimental results.

If everything goes well, all figures in the paper should show up in `eval/figures`.

> TODO: automate distributed experiments

## Artifact Claims

- Fig1
    - This figure compares the convergence time and network buildup time **in default runtime**(normal Linux and veth-pair setup)
    - Claim: the network buildup time should be 2-10 times longer than convergence time.
- Fig2
    - Each vertical line in this figure indicates a timestamps at which the node actually get scheduled on CPU (rather than waiting to be scheduled) **in default runtime**.
    - Claim: the vertical lines should be scattered throughout the later 75% or more of the entire time range.
- Fig3
    - Each point in this figure shows the number of routers that sends routing messages during some time interval (indicated by color) **in default runtime**.
    - Claim: the curve should be below the total 1125 nodes (the horizontal line at top) by about 50%.
- Fig9a-f:
    - These figures show the total time and memory usage to derive data plane of (1) emulation with REAL (REAL), (2) emulation with default runtime (Default), and (3) simulation with Batfish (Batfish).
    - Claims:
        1. REAL should be always faster than Default, comparable or faster than batfish;
        2. REAL may take slightly more memory than Defualt because it stores messages, but it should be within 20% and thus acceptable;
        3. For large topologies, REAL can finish them through iterative convergence while Batfish and Default runs out of memory.
- Fig10:
    - This figure compares the convergence time and network buildup time **between REAL and default runtime**.
    - Claim: REAL should have shorter or comparable convergence time, and much faster network buildup time.
- Fig11-13: TBD
- Fig14:
    - This figure shows the time-memory tradeoff with iterative convergence.
    - Claim: Memory could be decreased by 6x or more with increased number of partitions. The price of increased time is affordable (within 6x in most cases) and is worth the huge memory saving.
- Fig15:
    - This figure shows the time and memory usage for emulating ultra-large topologies using 2 distributed 64 core, 256GB machines with iterative convergence.
    - Claims:
        - We could utilize all 512GB memory
        - We could emulate topology as large as FT60 (4500 nodes)

## Basic Usage

### Run Emulations

Emulation parameters are configured in `run_config.yaml` by default. `test/basic_coverage/{baseline,preload}` contains some good starting points. The format is as below:

```yaml
# run_config.yaml template:
cores: ["all"]
topos: [["fattree", "2"]]
tag: test # appears in the results directory name to distinguish runs
image: frr # options: frr, crpd, bird
mode: baseline # options: baseline, preload
partitioned: false # enable iter-conv (partitioned convergence) or not
debug: false # enable debug (collects logs from libpreload). Note: impacts performance.

# (Optional) estimated convergence wait time (seconds). Script will wait up to this time, we should always over estimate this.
# This is mainly useful for baseline experiments.
# Preload experiments can automatically detect convergence and immediately stop, while it also respects this parameter in that if the system haven't converged after this amount of time, it will also stop. We should set this timer to a large value (say 1800s) and treat it as fallback plan to avoid indefinite waiting.
# Default: 60
time: 20

# (Optional) Whether to collect profiling data with perf.
# Default: false
# When enabled, eventchart and flamegraph are generated.
# Timebar, memory, and workset charts are always generated regardless of this option.
profile: false
```

Run an emulation experiment, note how we can indicate path to config file / directory containing config files:

```bash
./run.sh       # runs according to run_config.yaml
./run.sh test  # runs all YAML files under test/
./run.sh test/basic_coverage/preload/frr.yaml  # runs according to test/basic_coverage/preload/frr.yaml
```

`results/` and `pics/` directory should contain results of the experiments. The most important files are:

- `pics/<exp-id>/mem.png`: memory usage graph over time
- `pics/<exp-id>/timebar.png`: time breakdown graph
- `results/<exp-id>/node_logs/emu-real-1/bgp_routes-final.log`: all routes exported on node 1

### Distributed Emulation

Create a `hosts.json` file with the following format on all machines participated, note that `self_id` should correspond to the ip address of each machine:

```json
{
    "hosts": [
        {
            "id": 0,
            "ip": "10.60.93.122",
            "port": 26918
        },
        {
            "id": 1,
            "ip": "10.60.93.128",
            "port": 26919
        }
    ],
    "self_id": 0
}
```

The scripts will automatically find `hosts.json` and divide jobs between multiple machines. Then copy the `run_config.yaml` to every machine, ensure they're aligned. Finally simultanously start `./run.sh` on every machine.

### Run Batfish

Batfish parameters need to be specified in the run.sh command line.
The topology construction is already included in run.sh.  
Run an emulation experiment:

```bash
sudo bash -c 'source .venv/bin/activate; cd batfish; ./run.sh 64 fattree 10'
# Usage: ./run.sh <core> <topo_type> <topo_id>
#
# <core>     : Number of cores to use for execution.
# <topo_type> : Topology type (fattree, topozoo, dupzoo).
# <topo_id>   : Topology identifier (e.g., Kdl, Fccn:2).
#
# Examples:
# ./run.sh 64 topozoo Kdl
# ./run.sh 64 dupzoo Fccn:2
```
