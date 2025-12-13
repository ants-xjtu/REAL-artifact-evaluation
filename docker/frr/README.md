```bash
wget https://github.com/FRRouting/frr/archive/refs/tags/frr-10.1.4.tar.gz
tar -xzf frr-10.1.4.tar.gz
mv frr-frr-10.1.4 frr
docker build . -t real-frr --build-arg PROXY=http://10.181.168.232:7890
```

Reference: https://github.com/FRRouting/frr/blob/master/docker/ubuntu-ci/Dockerfile

Changes:
- Enable compiler flags `-g -O2 -fno-omit-frame-pointer` at build time.
- Use the Tsinghua (TUNA) APT mirror to speed up apt-get.
- Install `strace` and `ltrace`.
- Remove downloads related to [topotest](https://docs.frrouting.org/projects/dev-guide/en/latest/topotests.html), including its Python dependencies and exabgp.
- Allow using an HTTP proxy optionally when running `git clone`.