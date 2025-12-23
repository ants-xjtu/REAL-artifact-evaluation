#!/bin/bash

cleanup() {
    collect_info

    for name in $(ls /opt/lwc/containers/); do
        ./lwc/target/release/lwc stop ${name} &
    done
    wait

    save_logs ${results_dir}

    for name in $(ls /opt/lwc/containers/); do
        ./lwc/target/release/lwc remove ${name} &
    done

    for pid in $(ls /var/run/netns/); do
        if [ -e ./$pid ]; then
            echo $pid;
        else
            rm -rf /var/run/netns/$pid;
        fi
    done
}

collect_info() {
    python3 ./scripts/runtime/show_pids.py $image $n lwc > ${results_dir}/docker_to_pidalive
    for name in $(ls /opt/lwc/containers/ | head -n 1);
    do
        if [ "$image" == "frr" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            timeout -k 100 1 ./lwc/target/release/lwc exec ${name} bash -c "vtysh -c 'show ip bgp summary' &> /var/log/real/bgp_summary-final.log" || true
            ./lwc/target/release/lwc cp ${name}:/var/log/real/bgp_summary-final.log  "${results_dir}/node_logs/${name}/bgp_summary-final.log" || true
            timeout -k 100 1 ./lwc/target/release/lwc exec ${name} bash -c "vtysh -c 'show bgp all' &> /var/log/real/bgp_routes-final.log" || true
            ./lwc/target/release/lwc cp ${name}:/var/log/real/bgp_routes-final.log  "${results_dir}/node_logs/${name}/bgp_routes-final.log" || true
        elif [ "$image" == "crpd" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            ./lwc/target/release/lwc exec ${name} cli -c "show route" > ${results_dir}/node_logs/$name//bgp_routes-final.log || true
            ./lwc/target/release/lwc exec ${name} cli -c "show bgp summary" > ${results_dir}/node_logs/$name/bgp_summary-final.log || true
        elif [ "$image" == "bird" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            ./lwc/target/release/lwc exec ${name} bash -c "birdc show route &> /var/log/real/bgp_routes-final.log" || true
            ./lwc/target/release/lwc exec ${name} bash -c "birdc show protocols &> /var/log/real/bgp_summary-final.log" || true
            ./lwc/target/release/lwc cp ${name}:/var/log/real/bgp_routes-final.log "${results_dir}/node_logs/${name}/bgp_routes-final.log" || true
            ./lwc/target/release/lwc cp ${name}:/var/log/real/bgp_summary-final.log "${results_dir}/node_logs/${name}/bgp_summary-final.log" || true
        fi
    done
}

save_logs() {
    dir=$1
    echo $dir

    for name in $(ls /opt/lwc/containers/);
    do
        if [[ $image == bird ]]; then
            mkdir -p ${dir}/node_logs/${name}/
            (./lwc/target/release/lwc cp ${name}:/var/log/real/bird.log  "${dir}/node_logs/${name}/bird.log" || true) &
            (./lwc/target/release/lwc exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &
            (./lwc/target/release/lwc cp ${name}:/etc/bird/bird.conf "${dir}/node_logs/${name}/bird.conf" || true) &

            mkdir -p ${dir}/hjk_logs/${name}/
            ./lwc/target/release/lwc exec $name /bin/bash -c 'tar -cf - /var/log/real/preload_*' | tar -xf - -C "${dir}/hjk_logs/$name/" || true
        fi

        if [[ $image == frr ]]; then
            mkdir -p ${dir}/node_logs/${name}/
            (./lwc/target/release/lwc cp ${name}:/var/log/real/frr.log  "${dir}/node_logs/${name}/frr.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/var/log/real/strace.log  "${dir}/node_logs/${name}/strace.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/var/log/real/frrinit.log  "${dir}/node_logs/${name}/frrinit.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/etc/frr/frr.conf  "${dir}/node_logs/${name}/frr.conf" || true) &
            (./lwc/target/release/lwc exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &

            mkdir -p ${dir}/hjk_logs/${name}/
            ./lwc/target/release/lwc exec $name /bin/bash -c 'tar -cf - /var/log/real/preload_*' | tar -xf - -C "${dir}/hjk_logs/$name/" || true
        fi

        if [[ $image == crpd ]]; then
            mkdir -p ${dir}/node_logs/${name}/
            (./lwc/target/release/lwc cp ${name}:/etc/crpd/crpd.conf  "${dir}/node_logs/${name}/crpd.conf" || true) &
            (./lwc/target/release/lwc cp ${name}:/var/log/bgp.log  "${dir}/node_logs/${name}/bgp.log" || true) &
            (./lwc/target/release/lwc exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &
        fi
    done

    wait

    chmod a+rw -R ${dir}/*_logs/ || true
}