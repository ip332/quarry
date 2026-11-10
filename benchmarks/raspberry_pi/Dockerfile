# Static ARMv7 binaries avoid depending on the minimal target's glibc/C++ ABI.
FROM ubuntu:24.04
RUN dpkg --add-architecture armhf && \
    rm -f /etc/apt/sources.list.d/ubuntu.sources && \
    printf '%s\n' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble main universe' \
      'deb [arch=amd64] http://archive.ubuntu.com/ubuntu noble-updates main universe' \
      'deb [arch=amd64] http://security.ubuntu.com/ubuntu noble-security main universe' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble main universe' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble-updates main universe' \
      'deb [arch=armhf] http://ports.ubuntu.com/ubuntu-ports noble-security main universe' \
      > /etc/apt/sources.list && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf protobuf-compiler \
      libprotobuf-dev:armhf zlib1g-dev:armhf ca-certificates && \
    rm -rf /var/lib/apt/lists/*
