import time
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed


class CmdRecord:
    def __init__(self, cmd, node, t0, t1, result):
        self.ts_start = t0
        self.duration = t1 - t0
        self.cmd = cmd
        self.node = node
        self.r = result
        self.rc = result.returncode
        self.stdout = str(result.stdout) if result.stdout is not None else ""
        self.stderr = str(result.stderr) if result.stderr is not None else ""

    def __str__(self) -> str:
        s = f"{self.ts_start}: [{self.node}] {self.cmd}"
        if self.stdout:
            s += f"\nstdout: {self.stdout}"
        if self.stderr:
            s += f"\nstderr: {self.stderr}"
        return s + "\n"


def run_command(cmd, node=-1, timeout=None):
    t0 = time.monotonic()
    result = subprocess.run(
        cmd, shell=True, capture_output=True, text=True, timeout=timeout
    )
    t1 = time.monotonic()
    return CmdRecord(cmd, node, t0, t1, result)


def run_commands_for_routers(routers, get_cmd_list, *args, **kwargs):
    with ThreadPoolExecutor() as executor:
        futures = [
            executor.submit(
                lambda r: [
                    run_command(cmd, node=r["idx"])
                    for cmd in get_cmd_list(r, *args, **kwargs)
                ],
                router,
            )
            for router in routers
        ]
        records = []
        for future in as_completed(futures):
            records += future.result()
        return records


def run_func_for_routers(routers, func, *args, **kwargs):
    with ThreadPoolExecutor() as executor:
        futures = [executor.submit(func, router, *args, **kwargs) for router in routers]
        records = []
        for future in as_completed(futures):
            records += future.result()
        return records


class Runner:
    def __init__(self, image, topo, blueprint, local_nodes, output_dir):
        self.image = image
        self.topo = topo
        self.blueprint = blueprint
        self.local_nodes = local_nodes
        self.output_dir = output_dir

    def container_name(self, router):
        return f"emu-real-{router["idx"]}"

    def config_filename(self, router):
        return f"conf/{self.image}/{self.topo}/node_{router["idx"]}.conf"

    def ipc_path(self, router):
        return f"/opt/lwc/volumes/ripc/{self.container_name(router)}"

    def log_path(self, router):
        return f"/opt/lwc/containers/{self.container_name(router)}/rootfs/var/log/real"

    def env_dir(self, r):
        return f"{self.output_dir}/env/{self.container_name(r)}"

    def neigh_str(self, r):
        return ",".join(
            [
                f"{neigh["self_ip"]}:{neigh["neighbor_ip"]}:{neigh["peeridx"]}"
                for neigh in r.get("neighbors", [])
            ]
            + [""]  # trailing comma! Our C file checks for it.
        )

    def create_containers(self):
        return []

    def create_network(self):
        return []

    def start_daemons(self):
        return []

    def run_commands_forall(self, get_cmd_list):
        routers_filtered = [
            r for r in self.blueprint["routers"] if r["idx"] in self.local_nodes
        ]
        return run_commands_for_routers(routers_filtered, get_cmd_list)

    def run_func_forall(self, get_cmd_list):
        routers_filtered = [
            r for r in self.blueprint["routers"] if r["idx"] in self.local_nodes
        ]
        return run_func_for_routers(routers_filtered, get_cmd_list)
