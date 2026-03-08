# Build Setup

How to build and run NEOMACH from source.

## Quick Start

```sh
# 1. Install prerequisites (macOS)
brew install llvm qemu

# 2. Build the kernel
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
      -DNEOMACH_BOOT_TESTS=ON
cmake --build build

# 3. Run under QEMU
./tools/run-qemu.sh --no-build
```

## Prerequisites

NEOMACH uses Clang as a cross-compiler targeting `x86_64-unknown-elf`.

| Tool | Purpose | Minimum Version |
|------|---------|----------------|
| clang | C compiler (cross-compiles to x86-64 ELF) | 15.0 |
| ld.lld | LLVM linker (produces ELF binaries) | 15.0 |
| cmake | Build system | 3.20 |
| qemu-system-x86_64 | Emulator for testing | 7.0 |
| xorriso | ISO image creation (optional) | 1.5 |

### macOS (Homebrew)

```sh
brew install llvm qemu xorriso
```

### Debian / Ubuntu

```sh
sudo apt-get install clang lld qemu-system-x86 xorriso cmake
```

### Fedora / RHEL

```sh
sudo dnf install clang lld qemu-system-x86 xorriso cmake
```

### Nix (Recommended for Reproducibility)

```sh
nix develop
```

All tools are pinned to exact versions in `flake.nix`.

## Build Options

| CMake Option | Default | Description |
|-------------|---------|-------------|
| `NEOMACH_BOOT_TESTS` | OFF | Compile kernel boot self-tests (IPC smoke test) |
| `CMAKE_BUILD_TYPE` | - | Debug, Release, RelWithDebInfo |

## Build Commands

### Configure + Build

```sh
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
      -DCMAKE_BUILD_TYPE=Debug \
      -DNEOMACH_BOOT_TESTS=ON
cmake --build build
```

### Run Under QEMU

```sh
# Build and run (default)
./tools/run-qemu.sh

# Run without rebuilding
./tools/run-qemu.sh --no-build

# Force software emulation (no KVM)
./tools/run-qemu.sh --no-kvm
```

### Debug with GDB

In one terminal:
```sh
./tools/run-qemu.sh
```

In another:
```sh
./tools/debug-qemu.sh
# Or manually:
gdb build/neomach.elf -ex "target remote :1234"
```

## Output

The kernel binary is `build/neomach.elf` — a Multiboot2-compatible ELF that
QEMU can load directly with `-kernel`.

Serial output goes to stdout (COM1 at I/O port 0x3F8). Press `Ctrl-A X` to
quit QEMU.

## Toolchain Details

See [tools/cross/README.md](../tools/cross/README.md) for full cross-compiler
documentation.
