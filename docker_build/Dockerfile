# Cross compiles the Songodot SBC (arm64) export template on an x86_64 host.
# docker_build/build_sbc.sh builds and runs this, see README.md here.
#
# Matches the toolchain the earlier runtimes were built with (Ubuntu 20.04,
# clang/lld 10, GCC 10's static libstdc++), so the binary needs the same
# glibc (2.29) and works on the same CFWs. Don't move to a newer base without
# checking the oldest CFW you support.
#
# The arm64 libraries are installed as multiarch packages, so "/" works as the
# arm64 sysroot: libc and crt files in /usr/lib/aarch64-linux-gnu, GCC 10's
# runtime in /usr/lib/gcc/aarch64-linux-gnu/10, where clang looks for them.

FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# arm64 packages come from ports.ubuntu.com, so the main archive is limited
# to amd64
RUN dpkg --add-architecture arm64 \
    && sed -i 's/^deb /deb [arch=amd64] /' /etc/apt/sources.list \
    && printf '%s\n' \
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal main universe" \
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-updates main universe" \
        "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-security main universe" \
        > /etc/apt/sources.list.d/arm64-ports.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-10 \
        lld-10 \
        llvm-10 \
        binutils-aarch64-linux-gnu \
        pkg-config \
        python3 \
        python3-pip \
        rsync \
        ca-certificates \
        libc6-dev:arm64 \
        libgcc-10-dev:arm64 \
        libstdc++-10-dev:arm64 \
        zlib1g-dev:arm64 \
        libsdl2-dev:arm64 \
    && rm -rf /var/lib/apt/lists/*

# Godot 4.3 needs SCons 3.1.2+, use the same version as gdtaglib's image
RUN python3 -m pip install --no-cache-dir scons==4.7.0

# platform/sbc/detect.py calls plain clang/clang++, and looks for llvm-ar,
# llvm-ranlib and ld.lld on PATH
RUN cd /usr/local/bin \
    && ln -s /usr/bin/clang-10 clang \
    && ln -s /usr/bin/clang++-10 clang++ \
    && ln -s /usr/bin/ld.lld-10 ld.lld \
    && ln -s /usr/bin/llvm-ar-10 llvm-ar \
    && ln -s /usr/bin/llvm-ranlib-10 llvm-ranlib

# pkg-config must only see the arm64 .pc files (sdl2), not the host's. A
# wrapper rather than an ENV, since SCons' ParseConfig runs commands with its
# own environment, which doesn't pass PKG_CONFIG_* through but does have
# /usr/local/bin on PATH.
RUN printf '%s\n' \
        '#!/bin/sh' \
        'export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig' \
        'exec /usr/bin/pkg-config "$@"' \
        > /usr/local/bin/pkg-config \
    && chmod +x /usr/local/bin/pkg-config

COPY build_in_container.sh /usr/local/bin/build_in_container.sh

# detect.py cross compiles against SYSROOT_PATH
ENV SYSROOT_PATH=/
ENTRYPOINT ["/usr/local/bin/build_in_container.sh"]
