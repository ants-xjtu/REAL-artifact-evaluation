#!./.venv/bin/python3

import yaml
import subprocess
import sys
import shlex
import resource
import datetime
import os
from pathlib import Path

"""
# run_config.yaml template:
cores: ["0-15"]
topos: [["fattree", "2"]]
tag: test # shows up in result directory name, for distinguishing multile runs
image: frr # frr,crpd,bird
mode: baseline # baseline,preload
partitioned: false # enable iter-conv or not
debug: false # Enable debug or not, collects log from libpreload. This impacts performance.

# (Optional) estimated converge time (or longer)
# Default: 60
# The script will wait for this long for converge
time: 20

# (Optional) Whether to do profiling using perf.
# Default: false
# When enabled, eventchart and flamegraph are generated.
# Timebar, memory and workset charts are always generated, regardless of this option.
profile: false
"""


def set_file_descriptor_limit():
    try:
        with open("/proc/sys/fs/nr_open", "w") as f:
            f.write(str(1_000_000_000))
    except Exception as e:
        print(f"Warning: Failed to set file descriptor limit: {e}")

    resource.setrlimit(resource.RLIMIT_NOFILE, (1_000_000_000, 1_000_000_000))


def load_config(config_file):
    with open(config_file) as f:
        return yaml.safe_load(f)


def run_simulation(cfg):
    failed_runs = []
    cores_list = cfg.get("cores", [])
    topos = cfg.get("topos", [])
    tag = cfg.get("tag", "")
    image = cfg.get("image", "")
    mode = cfg.get("mode", "")
    wait_time = str(cfg.get("time", 60))
    debug = str(cfg.get("debug", False)) == "True"
    partitioned = cfg.get("partitioned", False)
    profile = str(cfg.get("profile", False)) == "True"

    for cores_str in cores_list:
        if cores_str == "all":
            cores = f"0-{(os.cpu_count() or 8)- 1}"
        else:
            cores = cores_str
        for t in topos:
            print("-" * 16)
            topo_type, topo_id = t
            confgen_cmd = ["./scripts/config/confgen.py", image, topo_type, topo_id]
            print("Running:", " ".join(shlex.quote(arg) for arg in confgen_cmd))
            subprocess.run(confgen_cmd, check=True)
            topo = get_topology_string(topo_type, topo_id)
            timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

            if partitioned:
                conf_path = f"conf/{image}/{topo}"
                if topo_type == "fattree":
                    cmd = [
                        "./scripts/config/fattree_division.py",
                        topo_id,
                        str(partitioned),
                        conf_path,
                    ]
                    print("Running:", " ".join(shlex.quote(arg) for arg in cmd))
                    subprocess.run(cmd, check=True)
                else:
                    cmd = ["make", "-C", "./scripts/config/", "clean"]
                    print("Running:", " ".join(shlex.quote(arg) for arg in cmd))
                    subprocess.run(cmd, check=True)
                    cmd = ["make", "-C", "./scripts/config/"]
                    print("Running:", " ".join(shlex.quote(arg) for arg in cmd))
                    subprocess.run(cmd, check=True)
                    cmd = ["./scripts/config/graph_division", str(partitioned), conf_path]
                    print("Running:", " ".join(shlex.quote(arg) for arg in cmd))
                    subprocess.run(cmd, check=True)

            cmd = [
                "./scripts/runtime/run_one.sh",
                "-i",
                image,
                "-T",
                topo,
                "-m",
                mode,
                "-t",
                tag,
                "-w",
                wait_time,
                "-x",
                timestamp,
                "-C",
                cores,
            ]

            if partitioned:
                cmd.append("-p")

            if debug:
                cmd.append("-D")

            if profile:
                cmd.append("-P")

            results_dir = f"./results/{mode}_{image}_{topo}_{tag}_{cores}_{timestamp}"
            pics_dir = f"./pics/{mode}_{image}_{topo}_{tag}_{cores}_{timestamp}"
            print("Running:", " ".join(shlex.quote(arg) for arg in cmd))
            print(f"{results_dir} {pics_dir}")
            subprocess.run(cmd, capture_output=True)

            verify_cmds = [
                [
                    "./scripts/test/check_bgp_summary.py",
                    "-d",
                    results_dir,
                    "-i",
                    image,
                    "-t",
                    topo,
                ],
                [
                    "./scripts/test/check_bgp_routes.py",
                    "-d",
                    results_dir,
                    "-i",
                    image,
                    "-t",
                    topo,
                ],
                [
                    "./scripts/test/check_charts.sh",
                    pics_dir,
                    mode,
                    "true" if profile else "false",
                ],
            ]

            failed = False
            for cmd in verify_cmds:
                try:
                    print(f"Verifying:", " ".join(shlex.quote(arg) for arg in cmd))
                    subprocess.run(cmd, capture_output=True, check=True)
                except subprocess.CalledProcessError as e:
                    print(
                        f"Verification failed for {results_dir}:"
                        + (f"\nstdout:\n{e.stdout.decode()}" if e.stdout else "")
                        + (f"\nstderr:\n{e.stderr.decode()}" if e.stderr else "")
                    )
                    failed = True

            if failed:
                print("[Failed]")
                failed_runs.append(results_dir)
            else:
                print("[Passed]")

    return failed_runs


def get_topology_string(topo_type, topo_id):
    if topo_type == "fattree":
        return f"fattree{topo_id}"
    elif topo_type == "topozoo":
        return f"topozoo_{topo_id}"
    elif topo_type == "dupzoo":
        topo_name, topo_copy = topo_id.split(":")
        return f"topozoo_{topo_name}_dup{topo_copy}"
    else:
        raise NotImplementedError(f"Unsupported topology type: {topo_type}")


def process_config_directory(config_dir):
    failed_runs = []
    config_dir = Path(config_dir)
    if not config_dir.is_dir():
        if not config_dir.is_file():
            raise ValueError(
                f"The provided path {config_dir} is not a valid directory/file."
            )
        else:
            cfg = load_config(config_dir)
            failed_runs += run_simulation(cfg)
    else:
        for config_file in config_dir.rglob("*.yaml"):
            cfg = load_config(config_file)
            failed_runs += run_simulation(cfg)

    return failed_runs


if __name__ == "__main__":
    set_file_descriptor_limit()
    config_dir = sys.argv[1] if len(sys.argv) > 1 else "./run_config.yaml"

    try:
        failed_runs = process_config_directory(config_dir)
        if failed_runs:
            print("The following runs failed:")
            for cfg in failed_runs:
                print(cfg)
            sys.exit(1)
        else:
            print("All runs processed successfully.")
            sys.exit(0)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
