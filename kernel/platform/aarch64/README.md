# kernel/platform/aarch64/

AArch64 (ARM64) Hardware Abstraction Layer for NEOMACH.

## Files

| File                | Description |
|---------------------|-------------|
| `boot.S`            | AArch64 boot entry point for QEMU `-machine virt`. Drops from EL2 to EL1, sets up the stack, zeros BSS, and calls `kernel_main()`. |
| `context_switch.S`  | AArch64 cooperative context switch. Saves/restores callee-saved registers (x19–x28, fp, sp, lr) per AAPCS64. |
| `platform.c`        | ARM PL011 UART driver for QEMU virt (base 0x09000000). Implements `platform_init()`, `serial_putchar()`, `serial_putstr()`, etc. |

## Building (from repo root)

```bash
# Prerequisites (macOS arm64 / M-series Mac):
brew install llvm qemu

# Build
cmake -S kernel -B build-aarch64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-elf-clang.cmake \
      -DNEOMACH_BOOT_TESTS=ON
cmake --build build-aarch64

# Run under QEMU
./tools/run-qemu.sh --arch=aarch64
```

## QEMU virt memory map (relevant regions)

| Address      | Device |
|--------------|--------|
| `0x09000000` | PL011 UART (→ `-serial stdio`) |
| `0x40000000` | RAM — kernel loaded here |

## AArch64 Bring-up Plan

- [x] EFI / QEMU virt boot entry (`boot.S`)
- [x] PL011 UART for console output (`platform.c`)
- [x] Cooperative context switch (`context_switch.S`)
- [ ] MMU initialization (4-level translation tables)
- [ ] `pmap.c` — AArch64 physical map
- [ ] GIC interrupt controller
- [ ] SMP trampoline for additional CPUs

## References

- ARM Architecture Reference Manual (ARMv8-A) — D1 Exception Model
- AAPCS64 — Procedure Call Standard for AArch64
- QEMU virt machine source: `hw/arm/virt.c`
- ARM PrimeCell UART (PL011) TRM (DDI 0183)
