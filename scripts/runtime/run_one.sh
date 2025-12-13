#!/bin/bash

set -x
ulimit -c unlimited
set +e
set -E

if [ -d ".venv" ]; then
    source .venv/bin/activate
fi

image=""
container_image=""
topo=""
mode=""
tag=""
cores=""
sched=""
bindcore=""
debug=false
partitioned=false
profile=false
wait_time=20
timestamp=""

while getopts "i:T:c:m:t:C:d:w:x:DsbpP" opt; do
    case $opt in
        i) image=$OPTARG ;;
        T) topo=$OPTARG ;;
        c) container_image=$OPTARG ;;
        m) mode=$OPTARG ;;
        t) tag=$OPTARG ;;
        C) cores=$OPTARG ;;
        w) wait_time=$OPTARG ;;
        x) timestamp=$OPTARG ;;
        D) debug=true ;;
        s) sched="-s" ;;
        b) bindcore="-b" ;;
        p) partitioned=true ;;
        P) profile=true ;;
        *) echo "Invalid option: -$opt" ; exit 1 ;;
    esac
done

if [ -z "$image" ] || [ -z "$topo" ] || [ -z "$mode" ] || [ -z "$tag" ] || [ -z "$cores" ]; then
    echo "Usage: $0 -i <image_name> -t <topo_name> -m <mode> -t <tag> -C <cores>"
    exit 1
fi

if [ -z "$timestamp" ]; then
    timestamp=$(date +%Y-%m-%d_%H-%M-%S)
fi

if [ "$image" = "srlinux" ]; then
    n=$(ls conf/$image/$topo | grep '^config' | wc -l)
else
    n=$(ls conf/$image/$topo | grep '.conf' | wc -l)
fi

perf_cores=$cores

results_dir=results/${mode}_${image}_${topo}_${tag}_${cores}_${timestamp}/
pics_dir=pics/${mode}_${image}_${topo}_${tag}_${cores}_${timestamp}/

mkdir -p $results_dir $pics_dir
chmod a+rw $results_dir $pics_dir

exec >> ${results_dir}/run.cmd 2>&1

echo 100000 > /proc/sys/net/core/somaxconn

mode_dir="scripts/runtime/${mode}"
if [ ! -d "${mode_dir}" ]; then
    echo "Error: Case ${mode} does not have an implementation."
    exit 1
fi

if [ -f "${mode_dir}/prepare.sh" ]; then
    source "${mode_dir}/prepare.sh"
    prepare
fi

if [ -f "${mode_dir}/run.sh" ]; then
    source "${mode_dir}/run.sh"
    run_case "$sched $bindcore"
fi

if [ -f "${mode_dir}/cleanup.sh" ]; then
    source "${mode_dir}/cleanup.sh"
    cleanup
fi

if [ -f "${mode_dir}/process.sh" ]; then
    source "${mode_dir}/process.sh"
    process_results $results_dir
fi

wait
