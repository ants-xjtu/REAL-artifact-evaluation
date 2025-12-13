#!/bin/bash

cleanup() {
    python3 ./scripts/runtime/show_pids.py $image $n lwc > ${results_dir}/docker_to_pidalive

    for name in $(ls /opt/lwc/containers/); do
        ./lwc/target/release/lwc stop ${name} &
    done
    wait

    save_logs ${results_dir}

    for name in $(ls /opt/lwc/containers/); do
        ./lwc/target/release/lwc remove ${name} &
    done
    rm -rf /opt/lwc/volumes/ripc/*
}

save_logs() {
    dir=$1
    echo $dir

    for name in $(ls /opt/lwc/containers/);
    do
        mkdir -p ${dir}/node_logs/${name}/
        (bash -c "cp /opt/lwc/containers/${name}/rootfs/var/log/real/bgp_* ${dir}/node_logs/${name}/" || true) &

        mkdir -p ${dir}/hjk_logs/${name}/
        (bash -c "cp /opt/lwc/containers/${name}/rootfs/var/log/real/preload* ${dir}/hjk_logs/${name}/" || true) &
        (bash -c "cp /opt/lwc/containers/${name}/rootfs/var/log/real/strace* ${dir}/hjk_logs/$name/" || true) &
        (bash -c "cp /opt/lwc/containers/${name}/rootfs/var/log/real/ltrace* ${dir}/hjk_logs/$name/" || true) &

        mkdir -p ${dir}/lwc/${name}/
        cp /opt/lwc/containers/${name}/config.json \
            /opt/lwc/containers/${name}/stderr.log \
            /opt/lwc/containers/${name}/stdout.log \
            ${dir}/lwc/${name}/

        if [[ $image == frr ]]; then
            (./lwc/target/release/lwc cp ${name}:/var/log/real/frr.log  "${dir}/node_logs/${name}/frr.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/var/log/real/frrinit.log  "${dir}/node_logs/${name}/frrinit.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/etc/frr/frr.conf  "${dir}/node_logs/${name}/frr.conf" || true) &
        fi

        if [[ $image == bird ]]; then
            (./lwc/target/release/lwc cp ${name}:/var/log/real/bird.log  "${dir}/node_logs/${name}/bird.log" || true) &
            (./lwc/target/release/lwc cp ${name}:/etc/bird/bird.conf "${dir}/node_logs/${name}/bird.conf" || true) &
        fi

        if [[ $image == crpd ]]; then
            (./lwc/target/release/lwc cp ${name}:/etc/crpd/crpd.conf  "${dir}/node_logs/${name}/crpd.conf" || true) &
            (./lwc/target/release/lwc cp ${name}:/var/log/bgp.log  "${dir}/node_logs/${name}/bgp.log" || true) &
        fi
    done
    wait

    chmod a+rw -R ${dir}/*_logs/
}