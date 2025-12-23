# Environment Setup Guide

> If you're from the NSDI'26 Artifact Evaluation Committee, we recommend you to use our already-built remote environment, see [remote environment access guide](./REMOTE_ACCESS.md) for details.

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