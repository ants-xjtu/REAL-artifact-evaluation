#!/bin/bash

prepare() {
    # IN cores
    # IN k
    # IN singlecore_cpuset
    # IN multicore_cpuset
    # OUT probe_event

    systemctl start docker.socket docker.service

    docker run -d --rm --name ${image}-preload-build real-${image} sleep infinity
    docker cp preload/ ${image}-preload-build:/usr/src/
    docker exec -u root ${image}-preload-build make -C /usr/src/preload clean

    if [ "$debug" == "true" ]; then
        make_flags+=" DEBUG=1"
    fi
    if [ "$image" == "crpd" ]; then
        make_flags+=" IMAGE_CRPD=1"
    fi
    if [ "$image" == "bird" ]; then
        make_flags+=" IMAGE_BIRD=1"
    fi
        
    docker exec -u root ${image}-preload-build make ${make_flags} -C /usr/src/preload
    docker cp ${image}-preload-build:/usr/src/preload/libpreload.so preload/libpreload.so
    docker rm -f ${image}-preload-build

    # TODO: build container fs with lwc or docker
    # cp preload/libpreload.so ${image}_fs/usr/lib/
    cp -r preload/ ${results_dir}/
    chmod a+rwx ${results_dir}/preload/

    cd lwc
    cargo build --release
    cd ..

    for name in $(ls /opt/lwc/containers); do
        (./lwc/target/release/lwc stop $name;
        ./lwc/target/release/lwc remove $name) &
    done
    rm -rf /opt/lwc/volumes/ripc/*

    wait

    MOUNT_POINT="/opt/lwc/volumes/ripc"
    SIZE="1G"

    sudo mkdir -p "$MOUNT_POINT"
    if mountpoint -q "$MOUNT_POINT"; then
        sudo umount "$MOUNT_POINT"
    fi
    sudo mount -t tmpfs -o size=$SIZE tmpfs "$MOUNT_POINT"

    probe_event=unix_stream_sendmsg
    perf probe --add ${probe_event} || true
}