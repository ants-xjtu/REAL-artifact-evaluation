from ..runner_base import Runner
from ..create_network import create_network_veth


class FrrBaseline(Runner):
    def create_containers(self):
        return self.run_commands_forall(
            lambda r: [
                f"./lwc/target/release/lwc create real-frr {self.container_name(r)}",
                f"./lwc/target/release/lwc cp {self.config_filename(r)} {self.container_name(r)}:/etc/frr/frr.conf",
                f"./lwc/target/release/lwc cp ./daemons {self.container_name(r)}:/etc/frr/daemons",
                f"./lwc/target/release/lwc start {self.container_name(r)} tini -- sleep infinity",
            ],
        )

    def create_network(self):
        return create_network_veth(self.blueprint)

    def start_daemons(self):
        return self.run_commands_forall(
            lambda r: [
                f'./lwc/target/release/lwc exec -d {self.container_name(r)} '
                + f"bash -c 'mkdir -p /var/log/real/; chmod 777 /var/log/real/; /usr/lib/frr/frrinit.sh start &> /var/log/real/frrinit.log'"
            ]
        )
