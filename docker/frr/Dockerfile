ARG UBUNTU_VERSION=24.04
FROM ubuntu:$UBUNTU_VERSION

ARG PROXY

RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources

# Update and install build requirements.
RUN apt update -y
RUN apt upgrade -y
# recycling zombie processes
RUN apt-get install -y tini
# Basic build requirements from documentation
RUN apt-get install -y \
        git autoconf automake libtool make libreadline-dev texinfo \
        pkg-config libpam0g-dev libjson-c-dev bison flex \
        libc-ares-dev python3-dev python3-sphinx \
        install-info build-essential libsnmp-dev perl \
        libcap-dev libelf-dev libunwind-dev \
        protobuf-c-compiler libprotobuf-c-dev libsqlite3-dev
# Libyang2 extra build requirements
RUN apt-get install -y \
        cmake \
        libpcre2-dev

# GRPC extra build requirements
RUN apt-get install -y \
        libgrpc-dev \
        libgrpc++-dev \
        protobuf-compiler-grpc

# Runtime/triage/testing requirements
RUN apt-get install -y \
        curl \
        gdb \
        kmod \
        iproute2 \
        iputils-ping \
        liblua5.3-dev \
        libssl-dev \
        lua5.3 \
        net-tools \
        python3 \
        python3-pip \
        sudo \
        time \
        tshark \
        valgrind \
        yodl \
        strace \
        ltrace

RUN groupadd -r -g 92 frr && \
    groupadd -r -g 85 frrvty && \
    adduser --system --ingroup frr --home /home/frr \
            --gecos "FRR suite" --shell /bin/bash frr && \
    usermod -a -G frrvty frr && \
    useradd -d /var/run/exabgp/ -s /bin/false exabgp && \
    echo 'frr ALL = NOPASSWD: ALL' | tee /etc/sudoers.d/frr && \
    mkdir -p /home/frr && chown frr.frr /home/frr

USER frr:frr

# build and install libyang2
RUN cd && pwd && ls -al && \
    https_proxy=$PROXY git clone https://github.com/CESNET/libyang.git && \
    cd libyang && \
    git checkout v2.1.128 && \
    mkdir build; cd build && \
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr \
          -DCMAKE_BUILD_TYPE:String="Release" .. && \
    make -j $(nproc) && \
    sudo make install

COPY --chown=frr:frr ./frr/ /home/frr/frr/

RUN cd ~/frr && \
    ./bootstrap.sh && \
    ./configure \
       --prefix=/usr \
       --sysconfdir=/etc \
       --localstatedir=/var \
       --sbindir=/usr/lib/frr \
       --enable-sharpd \
       --enable-multipath=64 \
       --enable-user=frr \
       --enable-group=frr \
       --enable-config-rollbacks \
       --enable-grpc \
       --enable-vty-group=frrvty \
       --enable-snmp=agentx \
       --enable-scripting \
       --with-pkg-extra-version=-my-manual-build \
       CFLAGS="-g -O2 -fno-omit-frame-pointer" && \
    make -j $(nproc) && \
    sudo make install

RUN cd ~/frr && make check || true

WORKDIR /home/frr/frr

RUN sudo install -m 775 -o frr -g frr -d /var/log/frr
RUN sudo install -m 775 -o frr -g frrvty -d /etc/frr
RUN sudo install -m 640 -o frr -g frrvty tools/etc/frr/vtysh.conf /etc/frr/vtysh.conf
RUN sudo install -m 640 -o frr -g frr tools/etc/frr/frr.conf /etc/frr/frr.conf
RUN sudo install -m 640 -o frr -g frr tools/etc/frr/daemons.conf /etc/frr/daemons.conf
RUN sudo install -m 640 -o frr -g frr tools/etc/frr/daemons /etc/frr/daemons
RUN sudo mkdir -p /frrque /var/lib/frr
RUN sudo chown frr:frr /var/lib/frr /frrque
RUN sudo chmod 777 /frrque

ENTRYPOINT ["/usr/bin/tini", "--"]

