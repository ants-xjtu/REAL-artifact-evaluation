ARG UBUNTU_VERSION=24.04
FROM ubuntu:$UBUNTU_VERSION

RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources && \
    sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list.d/ubuntu.sources

RUN apt-get update -y
RUN apt-get install -y \
        --no-install-recommends \
        --no-install-suggests \
        ca-certificates \
        wget \
        build-essential \
        flex \
        bison \
        libncurses-dev \
        libreadline-dev \
        libssh-dev \
        autoconf \
        iproute2 \
        libtinfo6 \
        libreadline8 \
        libssh-4 \
        strace \
        ltrace \
        procps \
        tini

COPY ./bird/ /usr/src/bird

RUN set -eux \
    && cd /usr/src/bird/ \
    && autoreconf \
    && ./configure \
        --prefix=/usr/ \
        --sysconfdir=/etc/bird/ \
        --localstatedir=/var/ \
        --enable-libssh \
    && make -j $(nproc) \
    && make install

ENTRYPOINT ["/usr/bin/tini", "--"]

CMD ["bird", "-f", "-R"]
