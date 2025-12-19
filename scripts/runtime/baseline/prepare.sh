#!/bin/bash

prepare() {
    # IN results_dir
    # OUT probe_event

    probe_event=tcp_sendmsg
    perf probe --add ${probe_event} || true
}