#!/usr/bin/env python3

import yaml

frr_template = """
name: emu
services:
  real:
    command: sleep infinity
    deploy:
      replicas: 5
    image: real-frr
    ipc: host
    network_mode: none
    privileged: true
    stdin_open: true
    tty: true
    volumes:
    - ripc:/ripc
    ulimits:
      core: -1
volumes:
  ripc: null
"""

crpd_template = """
name: emu
services:
  real:
    command: /sbin/runit-init.sh
    # command: sleep infinity
    deploy:
      replicas: 5
    image: real-crpd
    ipc: host
    network_mode: none
    privileged: true
    stdin_open: true
    tty: true
    volumes:
    - ripc:/ripc
    ulimits:
      core: -1
volumes:
  ripc: null
"""

srlinux_template = """
name: emu
services:
  real:
    command: /opt/srlinux/bin/sr_linux
    deploy:
      replicas: 5
    security_opt:
      - seccomp=unconfined
    environment:
      - SRLINUX=1
      - YANG_PUSH_ENABLE=1
    image: ghcr.io/nokia/srlinux:latest
    ipc: host
    network_mode: bridge
    privileged: true
    stdin_open: true
    tty: true
    volumes:
    - ripc:/ripc
    ulimits:
      core: -1
volumes:
  ripc: null
"""

bird_template = """
name: emu
services:
  real:
    command: sleep infinity
    deploy:
      replicas: 5
    image: real-bird
    ipc: host
    network_mode: none
    privileged: true
    stdin_open: true
    tty: true
    volumes:
    - ripc:/ripc
    ulimits:
      core: -1
volumes:
  ripc: null
"""


def gen_docker_compose(image, n, command=None, image_tag=None, volume=None):
    """Generate the docker-compose YAML file with given replicas."""
    # Load the YAML file
    if image == "frr":
        data = yaml.safe_load(frr_template)
    elif image == "crpd":
        data = yaml.safe_load(crpd_template)
    # elif image == "srlinux":
    #     data = yaml.safe_load(srlinux_template)
    elif image == "bird":
        data = yaml.safe_load(bird_template)
    else:
        print(f"unknown image {image}")
        exit(-1)

    # Update replicas
    if "services" in data:
        for service in data["services"].values():
            if "deploy" in service and "replicas" in service["deploy"]:
                service["deploy"]["replicas"] = n
            if image_tag is not None:
                service["image"] = f"{service['image']}:{image_tag}"
            if command is not None:
                service["command"] = command
            if "volumes" in service and volume is not None:
                service["volumes"] = [volume]

    # Save the updated YAML file
    with open("docker-compose.yml", "w") as file:
        yaml.safe_dump(data, file, default_flow_style=False)
