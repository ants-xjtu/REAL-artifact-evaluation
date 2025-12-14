#!/bin/bash

results_dir=$1
cpuset_index=$2
interval=1

skip=$(($cpuset_index + 6))

# Time CPU0 CPU1 CPU2 ... CPUn
header="Time"
for ((i=0; i<=$cpuset_index; i++)); do
    header="$header\tCPU$i"
done
echo -e "$header" > ${results_dir}/cpu.log

while true; do
	cpu=$(mpstat -P 0-$cpuset_index -u $interval 1 |  awk 'NR>'$skip' {printf("%f ",100-$NF) }; END { printf("\n")}')
	echo -e "$(date "+%H:%M:%S")\t$cpu" >> ${results_dir}/cpu.log
done