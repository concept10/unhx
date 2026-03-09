# tools/

Build tools, image creation, and development utilities for NEOMACH.

## Available Tools

| Tool | Status | Purpose |
|------|--------|---------|
| `run-qemu.sh` | ✅ Done | Launch QEMU with correct parameters for x86-64 or AArch64 |
| `qemu-run.sh` | ✅ Done | Alias / variant of QEMU launch script |
| `debug-qemu.sh` | ✅ Done | Launch QEMU with GDB stub for kernel debugging |
| `mirror-archives.sh` | ✅ Done | Mirror historical source archives to `archive/` |
| `cross/` | ✅ Done | Cross-compiler toolchain documentation (Clang/LLD) |
| `cmake/` | ✅ Done | CMake toolchain files (`x86_64-elf-clang.cmake`, `aarch64-elf-clang.cmake`) |
| `build.sh` | 🔲 Planned | Top-level build driver |
| `mkimage.sh` | 🔲 Planned | Create bootable disk image |

## Development Environment

Recommended: Nix flake (reproducible, no host contamination).

```sh
nix develop  # Enter NEOMACH dev shell
```

Alternative (macOS/Homebrew):

```sh
brew install llvm qemu xorriso
```

Alternative (Debian/Ubuntu):

```sh
sudo apt-get install clang lld qemu-system-x86 xorriso
```

## Quick Start

```sh
# Build kernel (x86-64)
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
      -DNEOMACH_BOOT_TESTS=ON
cmake --build build

# Run under QEMU
./tools/run-qemu.sh

# Debug with GDB
# Terminal 1: ./tools/run-qemu.sh  (already running)
./tools/debug-qemu.sh
```
