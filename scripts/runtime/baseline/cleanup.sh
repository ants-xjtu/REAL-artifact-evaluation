#!/bin/bash

cleanup() {
    collect_info

    save_logs ${results_dir}
    for pid in $(ls /var/run/netns/); do
        if [ -e ./$pid ]; then
            echo $pid;
        else
            rm -rf /var/run/netns/$pid;
        fi
    done

    docker compose down
}

collect_info() {
    python3 ./scripts/runtime/show_pids.py $image $n docker > ${results_dir}/docker_to_pidalive
    for name in $(docker ps | sed '1d' | awk '{print $NF}' | head -n 10);
    do
        if [ "$image" == "frr" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            timeout -k 10 1 docker exec ${name} bash -c "vtysh -c 'show ip bgp summary' &> /var/log/real/bgp_summary-final.log" || true
            docker cp ${name}:/var/log/real/bgp_summary-final.log  "${results_dir}/node_logs/${name}/bgp_summary-final.log" || true
            timeout -k 10 1 docker exec ${name} bash -c "vtysh -c 'show bgp all' &> /var/log/real/bgp_routes-final.log" || true
            docker cp ${name}:/var/log/real/bgp_routes-final.log  "${results_dir}/node_logs/${name}/bgp_routes-final.log" || true
        elif [ "$image" == "crpd" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            docker exec ${name} cli -c "show route" > ${results_dir}/node_logs/$name//bgp_routes-final.log || true
            docker exec ${name} cli -c "show bgp summary" > ${results_dir}/node_logs/$name/bgp_summary-final.log || true
        elif [ "$image" == "srlinux" ]; then
            mkdir -p ${results_dir}/srl_logs/$name/
            docker exec -u root ${name} bash -c "sr_cli -c 'show network-instance default route-table all' &> /var/log/real/bgp_routes-final.log" || true
            docker exec -u root ${name} bash -c "sr_cli -c 'show network-instance default route-table summary' &> /var/log/real/bgp_summary-final.log" || true
            docker cp ${name}:/var/log/real/bgp_routes-final.log "${results_dir}/srl_logs/${name}/bgp_routes-final.log" || true
            docker cp ${name}:/var/log/real/bgp_summary-final.log "${results_dir}/srl_logs/${name}/bgp_summary-final.log" || true
            # docker exec -u root ${name} sr_cli -c "show network-instance default route-table all" > ${results_dir}/srl_logs/$name//bgp_routes-final.log || true
            # docker exec -u root ${name} sr_cli -c "show network-instance default route-table summary" > ${results_dir}/srl_logs/$name//bgp_summary-final.log || true
        elif [ "$image" == "bird" ]; then
            mkdir -p ${results_dir}/node_logs/$name/
            docker exec ${name} bash -c "birdc show route &> /var/log/real/bgp_routes-final.log" || true
            docker exec ${name} bash -c "birdc show protocols &> /var/log/real/bgp_summary-final.log" || true
            docker cp ${name}:/var/log/real/bgp_routes-final.log "${results_dir}/node_logs/${name}/bgp_routes-final.log" || true
            docker cp ${name}:/var/log/real/bgp_summary-final.log "${results_dir}/node_logs/${name}/bgp_summary-final.log" || true
        fi
    done
}

save_logs() {
    dir=$1
    echo $dir

    if [[ $image == bird ]]; then
        for name in $(docker ps -a | sed '1d' | awk '{print $NF}');
        do
            mkdir -p ${dir}/node_logs/${name}/
            (docker cp ${name}:/var/log/real/bird.log  "${dir}/node_logs/${name}/bird.log" || true) &
            (docker exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &
            (docker cp ${name}:/etc/bird/bird.conf "${dir}/node_logs/${name}/bird.conf" || true) &

            mkdir -p ${dir}/hjk_logs/${name}/
            docker exec $name /bin/bash -c 'tar -cf - /var/log/real/preload_*' | tar -xf - -C "${dir}/hjk_logs/$name/" || true
        done
    fi

    if [[ $image == frr ]]; then
        for name in $(docker ps -a | sed '1d' | awk '{print $NF}');
        do
            mkdir -p ${dir}/node_logs/${name}/
            (docker cp ${name}:/var/log/real/frr.log  "${dir}/node_logs/${name}/frr.log" || true) &
            (docker cp ${name}:/var/log/real/strace.log  "${dir}/node_logs/${name}/strace.log" || true) &
            (docker cp ${name}:/var/log/real/frrinit.log  "${dir}/node_logs/${name}/frrinit.log" || true) &
            (docker cp ${name}:/etc/frr/frr.conf  "${dir}/node_logs/${name}/frr.conf" || true) &
            (docker exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &

            mkdir -p ${dir}/hjk_logs/${name}/
            docker exec $name /bin/bash -c 'tar -cf - /var/log/real/preload_*' | tar -xf - -C "${dir}/hjk_logs/$name/" || true
        done
    fi

    if [[ $image == crpd ]]; then
        for name in $(docker ps -a | sed '1d' | awk '{print $NF}');
        do
            mkdir -p ${dir}/node_logs/${name}/
            (docker cp ${name}:/etc/crpd/crpd.conf  "${dir}/node_logs/${name}/crpd.conf" || true) &
            (docker cp ${name}:/var/log/bgp.log  "${dir}/node_logs/${name}/bgp.log" || true) &
            (docker exec $name /bin/bash -c 'tar -cf - /var/log/real/strace.log*' | tar -xf - -C "${dir}/node_logs/$name/" || true) &
        done
    fi

    wait

    chmod a+rw -R ${dir}/*_logs/ || true
}