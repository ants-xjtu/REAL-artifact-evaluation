# README

```bash
wget https://bird.network.cz/download/bird-3.1.2.tar.gz
tar -xzf bird-3.1.2.tar.gz
mv bird-3.1.2 bird
docker build . -t real-bird
```

Reference: https://github.com/akafeng/docker-bird.git

Changes:
- Combined the builder and runtime stages so that libpreload.so can be built inside the image.
- Enabled `--enable-debug` at build time.
- Use the Tsinghua (TUNA) APT mirror to speed up apt-get.
- Install debugging and utility tools: `strace`, `ltrace`, `procps`, and `tini`.