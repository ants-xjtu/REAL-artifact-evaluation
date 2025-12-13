#!/bin/bash

prepare() {
    # IN results_dir
    # OUT probe_event

    systemctl start docker.socket docker.service

    local containers=$(docker ps -a | grep emu- | awk '{print $1;}')
    if [ -z "$containers" ]; then
        echo "No emu containers to remove."
    else
        docker rm -f $containers
    fi

    docker volume rm emu_ripc || true

    probe_event=tcp_sendmsg
    perf probe --add ${probe_event} || true
}