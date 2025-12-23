import time

from ..runner_base import Runner, run_command
from ..create_network import create_network_veth


class CrpdBaseline(Runner):
    def create_containers(self):
        return self.run_commands_forall(
            lambda r: [
                f"./lwc/target/release/lwc create real-crpd {self.container_name(r)}",
                f"./lwc/target/release/lwc cp {self.config_filename(r)} {self.container_name(r)}:/etc/crpd/crpd.conf",
                f"./lwc/target/release/lwc start {self.container_name(r)} /sbin/runit-init.sh",
                f"mkdir -p {self.log_path(r)}",
                f"chmod a+rwx -R {self.log_path(r)}",
            ]
        )

    def create_network(self):
        return create_network_veth(self.blueprint)

    def start_daemons(self):
        def boot_single_router(r):
            records = [
                run_command(
                    f"./lwc/target/release/lwc exec {self.container_name(r)} mkdir -p /var/license/"
                ),
                run_command(
                    f"./lwc/target/release/lwc cp ./crpd-license {self.container_name(r)}:/var/license/crpd-license"
                ),
            ]
            while True:
                rec = run_command(
                    f"./lwc/target/release/lwc exec {self.container_name(r)} cli -c 'request system license add /var/license/crpd-license'"
                    # f"docker exec {self.container_name(r)} strace -ttt -ff -o /strace.log cli -c 'request system license add /var/license/crpd-license'"
                )
                records.append(rec)
                if "add license complete" in rec.stdout:
                    break
                time.sleep(0.5)

            records.append(
                run_command(
                    f"./lwc/target/release/lwc exec {self.container_name(r)} bash -c 'cli << EOF\nconfig\nload override /etc/crpd/crpd.conf\ncommit\nEOF\n'"
                )
            )
            return records

        return self.run_func_forall(boot_single_router)
