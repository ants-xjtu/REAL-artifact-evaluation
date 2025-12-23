#!/bin/bash

process_results() {
    cd lwc && cargo build --release && cd ..
    ./lwc/target/release/lwc create real-${image} real-symfs
    ./lwc/target/release/lwc cp preload/libpreload.so real-symfs:/usr/lib/libpreload.so

    memchart
    timebar

    if [ "$profile" == false ]; then
        ./lwc/target/release/lwc remove real-symfs
        return
    fi
    # sched_events

    ./lwc/target/release/lwc remove real-symfs
    rm ${results_dir}/*.perf
}

memchart() {
    python3 ./scripts/analysis/memchart.py ${results_dir}/dynmem.log  ${results_dir}/ctrlmem.log ${pics_dir}/
}

timebar() {
    # Filter out perf_record_bpf_event and perf_record_ksymbol which perf script may fail to recognize
    python3 ./scripts/analysis/filter_perf.py ${results_dir}/perf_trace_${probe_event}.data ${results_dir}/perf_trace_${probe_event}_filtered.data
    mv ${results_dir}/perf_trace_${probe_event}_filtered.data ${results_dir}/perf_trace_${probe_event}.data
    perf script -f -i ${results_dir}/perf_trace_${probe_event}.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) --vmlinux=/usr/lib/debug/boot/vmlinux-$(uname -r) > ${results_dir}/trace_${probe_event}.perf || perf script -f -i ${results_dir}/perf_trace_${probe_event}.data --symfs=/opt/lwc/containers/real-symfs/rootfs/ --kallsyms=/boot/System.map-$(uname -r) > ${results_dir}/trace_${probe_event}.perf
    # Keep only ${probe_event} events from bgpd_io processes, remove those from bgpd
    ./scripts/analysis/eventfilter.pl --comm-filter=bgpd_io,bgpio-0-th,bgpMSched,bird --event-filter=probe:${probe_event} ${results_dir}/trace_${probe_event}.perf > ${results_dir}/trace_${probe_event}.perf.filtered
    mv ${results_dir}/trace_${probe_event}.perf.filtered ${results_dir}/trace_${probe_event}.perf

    # Extract the timestamp of the last probe:${probe_event}
    # local sleep_ts=$(cat ${results_dir}/netlink.ts)
    local sleep_ts=$(grep "probe:${probe_event}" ${results_dir}/trace_${probe_event}.perf | tail -n 1 | grep -E -o "[0-9]+\.[0-9]+")
    echo "sleep $sleep_ts" >> ${results_dir}/stage_border_ts

    # At this point we can draw a timebar that doesn't depend on perf_cycles.data
    ./scripts/analysis/timebar.py ${results_dir}/stage_border_ts ${pics_dir}/timebar.png
}
