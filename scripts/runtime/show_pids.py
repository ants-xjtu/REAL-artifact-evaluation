#!/usr/bin/env python3

import os
import sys
from collections import defaultdict


def build_process_hierarchy():
    """
    Build the system process tree, including all threads.
    Returns:
        process_tree: dict mapping parent PID to a list of child PIDs
    """
    process_tree = defaultdict(list)

    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        pid = int(entry)
        try:
            with open(f"/proc/{pid}/status", "r") as status_file:
                parent_pid = None
                for line in status_file:
                    if line.startswith("PPid:"):
                        parent_pid = int(line.split(":", 1)[1].strip())
                if parent_pid is not None:
                    process_tree[parent_pid].append(pid)

            task_dir = f"/proc/{pid}/task"
            if os.path.exists(task_dir):
                for thread in os.listdir(task_dir):
                    thread_tid = int(thread)
                    if thread_tid != pid:
                        process_tree[pid].append(thread_tid)

        except (FileNotFoundError, ValueError):
            continue

    return process_tree


def get_process_tree(pid, process_tree):
    """
    Get all child processes or threads for the specified PID.
    Args:
        pid: int, the target process PID
        process_tree: dict representing parent-child relationships
    Returns:
        list of PIDs including children and threads
    """
    processes = []
    stack = [pid]
    while stack:
        current_pid = stack.pop()
        processes.append(current_pid)
        stack.extend(process_tree.get(current_pid, []))
    return processes


def get_container_pid(container_name, container_type):
    """
    Get the main process PID of a container.
    Args:
        container_name: str, name of the container
        container_type: str, container type ('docker' or 'lwc')
    Returns:
        int, the container's main process PID
    """
    if container_type == "docker":
        command = f"docker inspect --format '{{{{.State.Pid}}}}' {container_name}"
    elif container_type == "lwc":
        command = f"cat /opt/lwc/containers/{container_name}/config.json | jq '.pid'"
    else:
        raise ValueError(f"Unsupported container type: {container_type}")

    try:
        pid = int(os.popen(command).read().strip())
        return pid
    except ValueError:
        raise RuntimeError(f"Failed to get PID for container {container_name}")


def process_multiple_containers(image_name, container_count, container_type):
    """
    Process multiple containers and print each container with its child
    processes and threads' PIDs.
    Args:
        container_count: int, number of containers
        container_type: str, container type ('docker' or 'lwc')
    """
    container_prefix = f"emu-real-"
    process_tree = build_process_hierarchy()

    for i in range(1, container_count + 1):
        container_name = f"{container_prefix}{i}"
        try:
            root_pid = get_container_pid(container_name, container_type)
            processes = get_process_tree(root_pid, process_tree)
            print(container_name, end=" ")
            for pid in sorted(processes):
                print(pid, end=" ")
            print("")
        except RuntimeError as e:
            print(f"Error processing {container_name}: {e}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python script.py <image_name> <container_count> <container_type>")
        sys.exit(1)

    try:
        image_name = sys.argv[1]
        container_count = int(sys.argv[2])
        container_type = sys.argv[3]
        process_multiple_containers(image_name, container_count, container_type)
    except ValueError:
        print("Invalid container count. Please provide a positive integer.")
        sys.exit(1)
