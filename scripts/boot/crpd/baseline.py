import time

from ..runner_base import Runner, run_command
from ..create_network import create_network_veth
from ..gen_docker_compose import gen_docker_compose


class CrpdBaseline(Runner):
    def create_containers(self):
        # example gen_docker_compose(command=):
        # "strace -D -ttt -ff -o /strace.log /sbin/runit-init.sh"
        gen_docker_compose(self.image, len(self.blueprint["routers"]))
        records = [run_command(f"docker compose up -d")]
        records += self.run_commands_forall(
            lambda r: [
                f"docker cp {self.config_filename(r)} {self.container_name(r)}:/etc/crpd/crpd.conf",
                f"docker exec -u root {self.container_name(r)} bash -c 'mkdir -p /var/log/real/; chmod 777 /var/log/real/'",
            ]
        )
        return records

    def create_network(self):
        return create_network_veth(self.image, self.blueprint)

    def start_daemons(self):
        def boot_single_router(r):
            records = [
                run_command(
                    f"docker exec {self.container_name(r)} mkdir -p /var/license/"
                ),
                run_command(
                    f"docker cp ./crpd-license {self.container_name(r)}:/var/license/crpd-license"
                ),
            ]
            while True:
                rec = run_command(
                    f"docker exec {self.container_name(r)} cli -c 'request system license add /var/license/crpd-license'"
                    # f"docker exec {self.container_name(r)} strace -ttt -ff -o /strace.log cli -c 'request system license add /var/license/crpd-license'"
                )
                records.append(rec)
                if "add license complete" in rec.stdout:
                    break
                time.sleep(0.5)

            records.append(
                run_command(
                    f"docker exec {self.container_name(r)} bash -c 'cli << EOF\nconfig\nload override /etc/crpd/crpd.conf\ncommit\nEOF\n'"
                )
            )
            return records

        return self.run_func_forall(boot_single_router)
