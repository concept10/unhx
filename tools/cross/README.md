# Cross-Compiler Toolchain for UNHU

UNHU targets `x86_64-unknown-elf` (bare-metal, no OS). The build uses
**LLVM/Clang** as a cross-compiler with `ld.lld` as the linker.

## Why Clang?

Unlike GCC, Clang is a native cross-compiler — a single binary can target any
architecture via `--target=`. No separate `x86_64-elf-gcc` build is needed.

## Prerequisites by Platform

### macOS (Homebrew)

```sh
brew install llvm qemu xorriso
```

Homebrew LLVM installs to `/opt/homebrew/opt/llvm/` (Apple Silicon) or
`/usr/local/opt/llvm/` (Intel). The CMake toolchain file
(`cmake/x86_64-elf-clang.cmake`) auto-detects both paths.

### Debian / Ubuntu

```sh
sudo apt-get install clang lld qemu-system-x86 xorriso
```

### Fedora / RHEL

```sh
sudo dnf install clang lld qemu-system-x86 xorriso
```

### Nix (Recommended)

```sh
nix develop
```

This drops you into a shell with all tools pinned to exact versions.
See `flake.nix` at the repository root.

## Toolchain File

The CMake toolchain file is at `cmake/x86_64-elf-clang.cmake`. It:

1. Finds the LLVM `clang` binary (brew paths, then system PATH)
2. Finds `ld.lld` (required — Apple's system linker cannot produce ELF)
3. Sets `--target=x86_64-unknown-elf` for all C and ASM compilation
4. Overrides CMake's link command to invoke `ld.lld` directly, avoiding
   macOS-specific linker flags

## Building

From the repository root:

```sh
cmake -S . -B build \
      -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-elf-clang.cmake \
      -DUNHU_BOOT_TESTS=ON
cmake --build build
```

## Running

```sh
./tools/run-qemu.sh
```

Or with the build step skipped:

```sh
./tools/run-qemu.sh --no-build
```

## Alternative: GCC Cross-Compiler

If you prefer GCC, build or install an `x86_64-elf` toolchain:

```sh
# macOS (Homebrew — third-party tap)
brew install x86_64-elf-gcc

# From source (any platform)
# See https://wiki.osdev.org/GCC_Cross-Compiler
```

Then pass a different toolchain file or set `CMAKE_C_COMPILER` manually.
The LLVM toolchain is recommended because it requires no cross-build step.
