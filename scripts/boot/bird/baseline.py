from ..runner_base import Runner, run_command
from ..create_network import create_network_veth
from ..gen_docker_compose import gen_docker_compose


class BirdBaseline(Runner):
    def create_containers(self):
        gen_docker_compose(self.image, len(self.blueprint["routers"]))
        records = [run_command(f"docker compose up -d")]
        records += self.run_commands_forall(
            lambda r: [
                f"docker cp {self.config_filename(r)} {self.container_name(r)}:/etc/bird/bird.conf",
                f"docker exec -u root {self.container_name(r)} bash -c 'mkdir -p /var/log/real/; chmod 777 /var/log/real/'",
            ]
        )
        return records

    def create_network(self):
        return create_network_veth(self.image, self.blueprint)

    def start_daemons(self):
        return self.run_commands_forall(
            lambda r: [f"docker exec {self.container_name(r)} bird"],
        )
