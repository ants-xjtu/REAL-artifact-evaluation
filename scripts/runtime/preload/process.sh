#!/bin/bash

process_results() {
    cd lwc && cargo build --release && cd ..
    ./lwc/target/release/lwc create real-${image} real-symfs
    ./lwc/target/release/lwc cp ${results_dir}/preload/libpreload.so real-symfs:/usr/lib/libpreload.so
    ./lwc/target/release/lwc cp ${results_dir}/controller/controller real-symfs:/$(pwd)/controller/controller

    memchart
    timebar

    if [ "$profile" == false ]; then
        ./lwc/target/release/lwc remove real-symfs
        return
    fi

    split_stages
    workset # workset depends on pid_to_dockername
    flamegraph
    # sched_events

    ./lwc/target/release/lwc remove real-symfs
    # rm ${results_dir}/*.perf
}

memchart() {
    python3 ./scripts/analysis/memchart.py ${results_dir}/dynmem.log  ${results_dir}/ctrlmem.log ${pics_dir}/
}

timebar() {
    # python3 ./scripts/analysis/idle_time.py ${results_dir}/converge_end_ts.txt ${results_dir}/switch_pods_ts.txt > ${results_dir}/idle_time.txt

    local sleep_ts=$(tail -n 1 ${results_dir}/converge_end_ts.txt)
    echo "sleep $sleep_ts" >> ${results_dir}/stage_border_ts

    # At this point we can draw a timebar that doesn't depend on perf_cycles.data
    ./scripts/analysis/timebar.py ${results_dir}/stage_border_ts ${pics_dir}/timebar.png

    cp ${results_dir}/stage_border_ts ${results_dir}/all_vlines

    touch ${results_dir}/converge_end_ts.txt
    touch ${results_dir}/switch_pods_ts.txt
    for ts in $(cat ${results_dir}/converge_end_ts.txt); do
        echo "switch_color $ts" >> ${results_dir}/all_vlines
    done
    for ts in $(cat ${results_dir}/switch_pods_ts.txt); do
        echo "switch_pod $ts" >> ${results_dir}/all_vlines
    done
}

workset() {
    ./scripts/analysis/workset.py --iolog_file ${results_dir}/io.log --pid_file ${results_dir}/pid_to_dockername -o ${pics_dir}/workset.png -t "Workset Curve"
}

split_stages() {
    python3 ./scripts/analysis/filter_perf.py ${results_dir}/perf_cycles.data ${results_dir}/perf_cycles_filtered.data
    mv ${results_dir}/perf_cycles_filtered.data ${results_dir}/perf_cycles.data

    perf script --show-namespace-events -f -i ${results_dir}/perf_cycles.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) --vmlinux=/usr/lib/debug/boot/vmlinux-$(uname -r) > ${results_dir}/cycles.perf || perf script --show-namespace-events -f -i ${results_dir}/perf_cycles.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) > ${results_dir}/cycles.perf
    ./scripts/analysis/pid2dockername.py ${results_dir}/cycles.perf ${results_dir}/docker_to_pidalive > ${results_dir}/pid_to_dockername

    # Generate cycles.perf without namespace events
    perf script -f -i ${results_dir}/perf_cycles.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) --vmlinux=/usr/lib/debug/boot/vmlinux-$(uname -r) > ${results_dir}/cycles.perf || perf script -f -i ${results_dir}/perf_cycles.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) > ${results_dir}/cycles.perf
    stages=$(./scripts/analysis/split_stages.py ${results_dir}/stage_border_ts ${results_dir}/cycles.perf ${results_dir})
}

flamegraph() {
    # Split perf records into stages
    echo "stages: $stages"

    # Generate flamegraphs and time charts for each stage
    for stage in $stages; do
        ./scripts/analysis/stackcollapse-perf.pl ${results_dir}/${stage}.perf > ${results_dir}/${stage}.folded
        if [ -s "${results_dir}/${stage}.folded" ]; then
            ./scripts/analysis/flamegraph.pl ${results_dir}/${stage}.folded --inverted > ${pics_dir}/${stage}_withidle.svg || true
            ./scripts/analysis/flamegraph.pl ${results_dir}/${stage}.folded --inverted --reverse > ${pics_dir}/${stage}_withidle_rev.svg
            if [ $(wc -l ${results_dir}/${stage}.folded | awk '{print $1}') -ne 1 ]; then
                sed -i '/^swapper/d' ${results_dir}/${stage}.folded
            fi
            ./scripts/analysis/flamegraph.pl ${results_dir}/${stage}.folded --inverted > ${pics_dir}/${stage}.svg
            ./scripts/analysis/flamegraph.pl ${results_dir}/${stage}.folded --inverted --reverse > ${pics_dir}/${stage}_rev.svg
            ./scripts/analysis/eventchart-pernode.py --pid_file ${results_dir}/pid_to_dockername --comm_filter bgpd vtysh swapper bird -o ${pics_dir}/${stage}_eventchart.png -t "Event Chart (${stage})" -g ${results_dir}/all_vlines ${results_dir}/${stage}.perf
            ./scripts/analysis/eventchart-percore.py --comm_filter swapper -o ${pics_dir}/${stage}_percore_eventchart.png -g ${results_dir}/all_vlines  ${results_dir}/${stage}.perf
        fi
    done
}

offcpu() {
    # cp ${results_dir}/offcpu.folded ${pics_dir}/offcpu.folded
    cp ./scripts/analysis/offcpu.html ${pics_dir}/offcpu.html
}

# sched_events() {
#     python3 ./scripts/analysis/filter_perf.py ${results_dir}/perf_sched.data ${results_dir}/perf_sched_filtered.data
#     mv ${results_dir}/perf_sched_filtered.data ${results_dir}/perf_sched.data

#     perf script -i ${results_dir}/perf_sched.data > ${results_dir}/sched.perf
#     ./scripts/analysis/sched_events.py ${results_dir}/sched.perf ${results_dir}/stage_border_ts bootup sleep > ${results_dir}/sched_bootup_to_sleep.txt
#     ./scripts/analysis/sched_events.py ${results_dir}/sched.perf ${results_dir}/stage_border_ts converge sleep > ${results_dir}/sched_converge.txt

#     python3 ./scripts/analysis/filter_perf.py ${results_dir}/perf_cache.data ${results_dir}/perf_cache_filtered.data
#     mv ${results_dir}/perf_cache_filtered.data ${results_dir}/perf_cache.data

#     perf script -i ${results_dir}/perf_cache.data > ${results_dir}/cache.perf
#     ./scripts/analysis/cache_stat.py ${results_dir}/cache.perf ${results_dir}/stage_border_ts bootup sleep > ${results_dir}/cache_bootup_to_sleep.txt
#     ./scripts/analysis/cache_stat.py ${results_dir}/cache.perf ${results_dir}/stage_border_ts converge sleep > ${results_dir}/cache_converge.txt
# }