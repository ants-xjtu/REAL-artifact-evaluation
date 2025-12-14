# README

## Setup

Tested only on Ubuntu 24.04; Ubuntu 24.04 is recommended.

### Docker

Do not install Docker directly with `apt install docker`; follow the official installation instructions instead: https://docs.docker.com/engine/install/ubuntu/#install-using-the-repository. Using the TUNA (Tsinghua University) mirror is recommended for faster downloads: https://mirrors.tuna.tsinghua.edu.cn/help/docker-ce/.

TODO: document required Docker version (affects image format).

Required packages

The runtime and build environment require:

- build-essential (gcc, g++, objdump, readelf, etc.)
- perf-related tools (linux-tools-generic)
- cargo (Rust package manager / compiler)

For debugging, it is recommended to also install:

- gdb
- strace

On Ubuntu 24.04 you can install the packages with:

```bash
apt-get install -y --no-install-recommends \
  build-essential make perl python3-venv python3-pip python3-dev virtualenv pipx \
  iproute2 iputils-ping netcat-openbsd tcpdump nmap traceroute dnsutils ethtool \
  nftables iptables jq ripgrep fd-find tree rsync gdb strace ltrace valgrind \
  unzip zip tar xz-utils vim tmux less nano htop iotop sysstat lsof \
  git git-lfs openssh-server ca-certificates gnupg software-properties-common apt-transport-https \
  linux-tools-common linux-tools-generic linux-headers-virtual rustc cargo curl \
  cloud-guest-utils lvm2 xfsprogs gdisk procps
```

#### Version requirements

Ubuntu 24 provides suitable versions by default; other distributions are untested.

- g++-13 or newer (for the `<format>` header)
- python3.12 (for type annotations)

### Configure Python virtual environment

```bash
# Create a virtual environment (assume the directory will be called .venv)
python3 -m venv .venv
# Activate the venv
source .venv/bin/activate
# Install the Python dependencies from requirements.txt
pip install -r requirements.txt
```

### Container images

Image names are hard-coded as `real-{image}`, e.g. `real-frr`, `real-crpd`, and `real-bird`. The images include compilers (gcc, etc.) so they can be used to build `libpreload.so` inside the container. The images also include debugging tools such as `strace` and `gdb`.

#### Pull prebuilt Docker images

```bash
docker pull wilsonxia/real-frr
docker tag wilsonxia/real-frr real-frr
docker pull wilsonxia/real-bird
docker tag wilsonxia/real-bird real-bird
docker pull batfish/allinone
```

##### crpd

CRPD images must be downloaded manually from Juniper's official site: https://www.juniper.net/documentation/us/en/software/crpd/crpd-deployment/topics/task/crpd-linux-server-install.html. Save the image archive as `crpd.tar`.

Obtain your CRPD license from Juniper and save it as `crpd-license`.

```bash
docker load -i crpd.tar
# `docker image ls` will show the name and tag of the loaded image;
# retag it as appropriate (version may vary):
docker tag crpd:23.2R1.13 real-crpd:origin
# use the Dockerfile to install the correct toolchain for compiling libpreload.so
cd docker/crpd
docker build . -t real-crpd
```

#### LWC images

To prepare images for the LWC runtime environment:

```bash
mkdir -p /opt/lwc/{image,containers,volumes,layers}
images=(frr crpd bird)
for img in "${images[@]}"; do
    sudo docker image save real-${img} -o real-${img}.tar
    mkdir -p /opt/lwc/image/real-${img}
    ./scripts/utils/lwc_load_img.sh real-${img}.tar real-${img}
done
```

## Usage

### Generate configuration files

```bash
# Example: generate config for the frr image and a fattree topology of size 2
./scripts/config/confgen.py frr fattree 2
```

### Run simulations

Simulation parameters are configured in `run_config.yaml`.

```yaml
# run_config.yaml template:
cores: ["0-15"]
topos: [["fattree", "2"]]
tag: test # appears in the results directory name to distinguish runs
image: frr # options: frr, crpd, bird
mode: baseline # options: baseline, preload
partitioned: false # enable iter-conv (partitioned convergence) or not
debug: false # enable debug (collects logs from libpreload). Note: impacts performance.

# (Optional) estimated convergence wait time (seconds). Script will wait up to this time.
# Default: 60
time: 20

# (Optional) Whether to collect profiling data with perf.
# Default: false
# When enabled, eventchart and flamegraph are generated.
# Timebar, memory, and workset charts are always generated regardless of this option.
profile: false
```

Run an emulation experiment:

```bash
sudo su
# run.py requires Python packages, so ensure the virtualenv is activated
source .venv/bin/activate
./run.py       # runs according to run_config.yaml
./run.py test  # runs all YAML files under test/
```

Distributed experiments: create a `hosts.json` file with the following format:

```json
{
    "hosts": [
        {
            "id": 0,
            "ip": "10.60.93.122",
            "port": 26918
        },
        {
            "id": 1,
            "ip": "10.60.93.128",
            "port": 26919
        }
    ],
    "self_id": 0
}
```

### Generate Batfish configuration files

```bash
cd batfish
# Example: generate config for the frr image and a fattree topology of size 2
./scripts/config/confgen.py frr fattree 2
```

### Run Batfish
Batfish parameters need to be specified in the run.sh command line.

Run an emulation experiment:

```bash
source .venv/bin/activate
cd batfish
sudo ./run.sh 32 fattree 10
# sudo ./run.sh 32 topozoo Kdl
# sudo ./run.sh 32 dupzoo Fccn:2
#./run.sh <core> <topo_type> <topo_id>
# <core> represents the number of cores to use for execution
# <topo_type> and <topo_id> have the same meanings as the corresponding parameters in confgen.py
```