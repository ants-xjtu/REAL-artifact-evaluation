#!/bin/bash
for pid in $(ls /var/run/netns/); do
    if [ -e ./$pid ]; then
        echo $pid;
    else
        rm -rf /var/run/netns/$pid;
    fi
done
docker rm -f $(docker ps -a | grep emu-real | awk '{print $1;}')
echo "docker rm done"
rm -rf /dev/shm/port-*
docker volume rm emu_ripc
kill $(pgrep perf)
for pid in $(ls /var/run/netns/); do
    if [ -e ./$pid ]; then
        echo $pid;
    else
        rm -rf /var/run/netns/$pid;
    fi
done

delete_probe_event() {
    local probe_event=$1
    delete_successful=false

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

delete_probe_event tcp_sendmsg
delete_probe_event unix_stream_sendmsg

# lwc containers
pgrep monitor | xargs kill
pgrep controller | xargs kill
pgrep tini | xargs kill
pgrep sleep | xargs kill
pgrep bird | xargs kill
for name in $(ls /opt/lwc/containers); do
    (
        ./lwc/target/release/lwc stop ${name} || true;
        ./lwc/target/release/lwc remove ${name} || true;
        umount -R /opt/lwc/containers/$name/rootfs || true;
        rm -rf /opt/lwc/containers/$name || true
    ) &
done
rm -rf /opt/lwc/volumes/ripc/*