import time

from ..runner_base import Runner


class FrrPreload(Runner):
    def create_containers(self):
        base_ts = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        rt_base_ts = time.clock_gettime_ns(time.CLOCK_REALTIME)
        mono_raw_base_ts = time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)
        return self.run_commands_forall(
            lambda r: [
                f"./lwc/target/release/lwc create real-frr {self.container_name(r)} -v ripc:/ripc",
                # env file
                f"mkdir -p {self.env_dir(r)}",
                f"bash -c 'echo -e \"NODE_ID={r['idx']}\\nPEER_LIST={self.neigh_str(r)}\\nBASE_TS={base_ts}\\nRT_BASE_TS={rt_base_ts}\\nMONO_RAW_BASE_TS={mono_raw_base_ts}\"' > {self.env_dir(r)}/real_env",
                f"./lwc/target/release/lwc cp {self.env_dir(r)}/real_env {self.container_name(r)}:/real_env",
                # preload lib and config
                f"./lwc/target/release/lwc cp ./preload/libpreload.so {self.container_name(r)}:/usr/lib/libpreload.so",
                f"./lwc/target/release/lwc cp ./preload/ld.so.preload {self.container_name(r)}:/etc/ld.so.preload",
                f"./lwc/target/release/lwc cp {self.config_filename(r)} {self.container_name(r)}:/etc/frr/frr.conf",
                f"mkdir -p {self.ipc_path(r)} {self.log_path(r)}",
                f"chmod a+rwx -R {self.ipc_path(r)} {self.log_path(r)}",
                # finally start
                f"./lwc/target/release/lwc start {self.container_name(r)} tini -- sleep infinity",
            ],
        )
