import sys
import time
import argparse
import json
from pathlib import Path

from .bird.baseline import BirdBaseline
from .bird.preload import BirdPreload
from .crpd.baseline import CrpdBaseline
from .crpd.preload import CrpdPreload
from .frr.baseline import FrrBaseline
from .frr.preload import FrrPreload
from .runner_base import Runner

_REGISTRY = {
    ("bird", "baseline"): BirdBaseline,
    ("bird", "preload"): BirdPreload,
    ("crpd", "baseline"): CrpdBaseline,
    ("crpd", "preload"): CrpdPreload,
    ("frr", "baseline"): FrrBaseline,
    ("frr", "preload"): FrrPreload,
}


def main():
    parser = argparse.ArgumentParser(description="Your program description")

    parser.add_argument("-i", "--image", type=str, required=True, help="Image name")
    parser.add_argument("-m", "--mode", type=str, required=True, help="Mode name")
    parser.add_argument("-t", "--topo", type=str, required=True, help="Topology name")
    parser.add_argument(
        "-o", "--output", type=str, required=True, help="Output directory"
    )
    parser.add_argument(
        "-p", "--partitioned", action="store_true", required=False, help="Partitioned"
    )
    parser.add_argument(
        "-c", "--container-image", type=str, required=False, help="Container image name"
    )

    args = parser.parse_args()
    image = args.image
    mode = args.mode
    topo = args.topo
    output_dir = args.output
    partitioned = args.partitioned

    with open(f"conf/{image}/{topo}/blueprint.json") as f:
        blueprint = json.load(f)

    if partitioned:
        with open(f"conf/{image}/{topo}/partition.json") as f:
            parts = json.load(f)
    else:
        parts = [[i + 1 for i in range(len(blueprint["routers"]))]]

    path = Path("hosts.json")
    if path.exists() and mode == "preload":
        with path.open("r", encoding="utf-8") as f:
            hosts = json.load(f)
    else:
        hosts = {"hosts": [{"id": 0, "ip": "0.0.0.0", "port": 0}], "self_id": 0}
    nhosts = len(hosts["hosts"])

    local_nodes = set()
    for part in parts:
        part_size = len(part)
        perhost_size = (part_size + nhosts - 1) / nhosts
        host_idx = 0
        currhost_size = 0
        for u in sorted(part):
            if host_idx == hosts["self_id"]:
                local_nodes.add(u)
            currhost_size += 1
            if currhost_size >= perhost_size:
                host_idx += 1
                currhost_size = 0

    runner: Runner = _REGISTRY[(image, mode)](
        image, topo, blueprint, local_nodes, output_dir
    )

    ts_logs = []
    records = []

    time_create_containers = time.monotonic()
    ts_logs += [f"create_containers {time_create_containers}"]
    records += runner.create_containers()

    time_create_network = time.monotonic()
    ts_logs += [f"create_network {time_create_network}"]
    records += runner.create_network()

    time_start_daemons = time.monotonic()
    ts_logs += [f"converge {time_start_daemons}"]
    records += runner.start_daemons()

    time_end = time.monotonic()

    for log in ts_logs:
        print(log)

    # print to stderr, keep stage_border_ts clean
    sys.stdout = sys.stderr
    print("cost time:")
    print(f"\tbootup:{time_create_network - time_create_containers} s")
    print(f"\tcreate_network:{time_start_daemons - time_create_network} s")
    print(f"\tstart_daemons:{time_end - time_start_daemons} s")

    with open(f"{output_dir}/runner.cmd", "w") as f:
        f.writelines([str(rec) for rec in records])


if __name__ == "__main__":
    main()
