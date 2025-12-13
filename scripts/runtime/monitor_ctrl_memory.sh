#!/bin/bash

results_dir=$1
ctrl_pid=$2
interval=1

while :
do
    date "+%H:%M:%S" >> ${results_dir}/ctrlmem.log
    grep -E "VmRSS" /proc/${ctrl_pid}/status >> ${results_dir}/ctrlmem.log
    sleep $interval
done