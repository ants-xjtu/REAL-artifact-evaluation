#!/bin/bash

prepare() {
    # IN results_dir
    # OUT probe_event

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

    probe_event=tcp_sendmsg
    perf probe --add ${probe_event} || true
}