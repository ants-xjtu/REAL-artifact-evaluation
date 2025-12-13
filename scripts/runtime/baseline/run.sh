#!/bin/bash

run_case() {
    # IN probe_event
    # IN perf_cores
    # IN wait_time

    ###################################
    # Start perf
    ###################################

    # start perf record in background
    bg_pids=()
    perf record --namespaces -k CLOCK_MONOTONIC -F 299 -e cpu-clock -C ${perf_cores} -g -o ${results_dir}/perf_cycles.data &
    bg_pids+=($!)
    perf record -k CLOCK_MONOTONIC --overwrite -e probe:${probe_event} -o ${results_dir}/perf_trace_${probe_event}.data &
    bg_pids+=($!)
    # perf record -e context-switches,cpu-migrations -C ${perf_cores} -o ${results_dir}/perf_sched.data &
    # bg_pids+=($!)
    # perf record -F 299 -e L1-dcache-load-misses,L1-dcache-loads,LLC-loads,LLC-load-misses,dTLB-load-misses,dTLB-loads,iTLB-load-misses,instructions -C ${perf_cores} -o ${results_dir}/perf_cache.data &
    # bg_pids+=($!)
    sleep 10

    ###################################
    # Run the case, wait for converge
    ###################################
    mkdir -p ${results_dir}/config
    cp -r ./conf/${image}/${topo} ${results_dir}/config

    ./scripts/runtime/monitor_memory.sh ${results_dir} &
    local monitor_pid=$!

    taskset -c ${perf_cores} python3 -m scripts.boot -i $image -m $mode -t $topo ${container_image:+-c "$container_image"} -o ${results_dir} > ${results_dir}/stage_border_ts
    sleep $wait_time

    ###################################
    # Stop perf
    ###################################
    kill $monitor_pid
    kill ${bg_pids[@]}

    delete_probe_event
}

delete_probe_event() {
    delete_successful=false

    while [ $delete_successful == false ]; do
        if perf probe --del ${probe_event} 2>/tmp/perf_del_error.log; then
            echo "Successfully deleted event: ${probe_event}"
            delete_successful=true
        else
            if grep -q "does not hit any event" /tmp/perf_del_error.log; then
                echo "Event ${probe_event} does not exist. Exiting loop."
                delete_successful=true
            else
                sleep 1
            fi
        fi
    done
}