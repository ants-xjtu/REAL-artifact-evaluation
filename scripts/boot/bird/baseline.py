from ..runner_base import Runner
from ..create_network import create_network_veth


class BirdBaseline(Runner):
    def create_containers(self):
        return self.run_commands_forall(
            lambda r: [
                f"./lwc/target/release/lwc create real-bird {self.container_name(r)}",
                f"./lwc/target/release/lwc cp {self.config_filename(r)} {self.container_name(r)}:/etc/bird/bird.conf",
                f"./lwc/target/release/lwc start {self.container_name(r)} tini -- sleep infinity",
                f"./lwc/target/release/lwc exec {self.container_name(r)} bash -c 'mkdir -p /var/log/real/; chmod 777 /var/log/real/'",
            ]
        )

    def create_network(self):
        return create_network_veth(self.blueprint)

    def start_daemons(self):
        return self.run_commands_forall(
            lambda r: [f"./lwc/target/release/lwc exec {self.container_name(r)} bird"],
        )
