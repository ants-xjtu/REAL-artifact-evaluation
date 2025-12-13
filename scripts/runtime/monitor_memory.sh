#!/bin/bash

results_dir=$1
interval=1

echo -e "Time\t\tTotal\tUsed\tFree\tAvailable" >> ${results_dir}/dynmem.log

while :
do
    echo -e "$(date "+%H:%M:%S")\n$(free -h)" >> ${results_dir}/dynmem.log
    sleep $interval
done