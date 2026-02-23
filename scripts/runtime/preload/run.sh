#!/bin/bash

run_case() {
    # IN sched_bindcore

    # OUT controller_pid

    local sched_bindcore=$1

    ###################################
    # Start perf
    ###################################

    # start perf record in background
    bg_pids=()
    perf record --namespaces -k CLOCK_MONOTONIC -F 99 -e cpu-clock -C ${perf_cores} -g -o ${results_dir}/perf_cycles.data &
    bg_pids+=($!)

    sleep 10
    ###################################
    # Run the case, wait for converge
    ###################################

    make -C controller clean
    ctrl_flags=""
    if [ "$debug" == "true" ]; then
        ctrl_flags="$ctrl_flags DEBUG=1"
        mkdir ${results_dir}/ctrl/
    fi
    boot_flags=""
    if [ "$partitioned" == "true" ]; then
        ctrl_flags="$ctrl_flags ITER_CONV=1"
        boot_flags="$boot_flags -p"
    fi

    # Handle sched_mode -> build flags
    case "$sched_mode" in
        baseline)
            ctrl_flags="$ctrl_flags R2I_DISABLED=1 TWO_PHASE_DISABLED=1"
            ;;
        improved)
            # No flags - R2I enabled, 2-phase enabled (default behavior)
            ;;
        default|*)
            ctrl_flags="$ctrl_flags R2I_DISABLED=1"
            ;;
    esac

    make ${ctrl_flags} -C controller
    cp -r ./controller/ ${results_dir}/controller/
    chmod a+rwx ${results_dir}/controller/

    mkdir -p ${results_dir}/config
    cp -r ./conf/${image}/${topo} ${results_dir}/config
    ./scripts/runtime/monitor_memory.sh ${results_dir} &
    local monitor_pid=$!

    taskset -c ${perf_cores} python3 -m scripts.boot -i $image -m $mode -t $topo ${container_image:+-c "$container_image"} ${boot_flags} -o ${results_dir} > ${results_dir}/stage_border_ts

    # Let the great world spin!
    sync
    taskset -c ${perf_cores} chrt 20 ./controller/controller $image $topo ${results_dir} $(nproc) $wait_time hosts.json &> ${results_dir}/controller.log &
    local ctrl_pid=$!

    ./scripts/runtime/monitor_ctrl_memory.sh ${results_dir} ${ctrl_pid} &
    local monitor_ctrl_pid=$!

    perf stat -C ${perf_cores} -e L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./scripts/runtime/monitor_pid.sh $ctrl_pid &> ${results_dir}/cache_stat.log &

    wait ${ctrl_pid}
    kill $monitor_pid $monitor_ctrl_pid
    ###################################
    # Stop perf
    ###################################
    kill ${bg_pids[@]}
    delete_probe_event
}

delete_probe_event() {
    local delete_successful=false

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
