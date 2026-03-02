FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    clang \
    lld \
    llvm \
    cmake \
    ninja-build \
    make \
    qemu-system-x86 \
    xorriso \
    grub-pc-bin \
    grub-common \
    gdb \
    git \
    ca-certificates \
    pkg-config \
    python3 \
    && rm -rf /var/lib/apt/lists/*

USER vscode
WORKDIR /workspaces/unhx
