# tools/

Build tools, image creation, and development utilities for UNHU.

## Planned Tools

| Tool | Purpose |
|------|---------|
| `build.sh` | Top-level build driver |
| `cmake/` | CMake modules for cross-compilation |
| `nix/` | Nix flake for reproducible dev environment |
| `mkimage.sh` | Create bootable disk image |
| `qemu-run.sh` | Launch QEMU with correct parameters |
| `cross/` | Cross-compiler toolchain setup |

## Development Environment

Recommended: Nix flake (reproducible, no host contamination).

```sh
nix develop  # Enter UNHU dev shell
```

Alternative: Install manually:
- `x86_64-elf-gcc` cross-compiler
- `nasm` assembler
- `qemu-system-x86_64`
- `grub-mkrescue` (Multiboot2 image creation)

## Quick Start (once kernel/ has initial code)

```sh
cd tools
./build.sh           # Build kernel
./qemu-run.sh        # Boot under QEMU
```
