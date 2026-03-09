# kernel/platform/

Hardware Abstraction Layer (HAL) for NEOMACH kernel.

## Targets

| Directory   | Architecture | Priority |
|-------------|-------------|----------|
| `x86_64/`   | AMD64 / Intel 64 | Primary (Phase 1) |
| `aarch64/`  | ARM64 / Apple Silicon | Secondary (Phase 2+) |

The x86-64 implementation files live in this directory (`kernel/platform/`).
The `x86_64/` subdirectory is reserved for future architecture-specific
additions that need to be isolated from the AArch64 build.

## Files (x86-64, this directory)

| File                  | Description |
|-----------------------|-------------|
| `boot.S`              | Multiboot1 boot entry (32-bit PM → 64-bit long mode). Sets up identity-mapped boot page tables, enables PAE and paging, establishes a 16 KB stack, and calls `kernel_main()`. |
| `gdt.c` / `gdt.h`     | Three-entry GDT (null, ring-0 code, ring-0 data). `gdt_init()` builds the table, loads GDTR, and reloads all segment registers via far-return. |
| `paging.c` / `paging.h` | 4-level page tables (PML4 → PDPT → PD → PT). `paging_init()` creates a 4 MB identity map and a higher-half kernel mapping at `0xFFFFFFFF80000000`, then loads CR3. `paging_map()` dynamically adds 4 KB PTEs using `vm_page_alloc()`. |
| `platform.c` / `platform.h` | NS16550-compatible UART driver (COM1, 0x3F8, 115200 baud 8N1). Provides `platform_init()`, `serial_putchar()`, `serial_putstr()`, `serial_puthex()`, and `serial_putdec()`. |
| `context_switch.S`    | Cooperative context switch. Saves and restores the six callee-saved GPRs (RBX, RBP, R12–R15), RSP, and RIP per the System V AMD64 ABI. |

## Files (AArch64, `aarch64/` subdirectory)

| File                        | Description |
|-----------------------------|-------------|
| `aarch64/boot.S`            | Boot entry for QEMU `-machine virt`. Drops from EL2 to EL1, sets up a 16 KB stack, zeros BSS, and calls `kernel_main()`. |
| `aarch64/platform.c`        | ARM PL011 UART driver (base `0x09000000`, 115200 baud). Implements the same `platform_init()` / `serial_*()` API as the x86-64 build. |
| `aarch64/context_switch.S`  | AArch64 cooperative context switch. Saves and restores x19–x28, fp (x29), sp, and lr (x30) per AAPCS64. |

## x86-64 Bring-up Plan

- [x] Multiboot1 boot entry point with AOUT_KLUDGE (`boot.S`)
- [x] GDT setup — null, ring-0 code (L=1), ring-0 data (`gdt.c`)
- [x] 4-level page table initialization — identity map + higher-half kernel (`paging.c`)
- [x] NS16550 serial UART driver (COM1, 115200 baud) with hex/decimal helpers (`platform.c`)
- [x] Cooperative context switch — callee-saved GPRs + RSP/RIP (`context_switch.S`)
- [ ] IDT setup and exception/interrupt handlers
- [ ] APIC initialization (local APIC, IOAPIC)
- [ ] `pmap.c` — x86-64 physical map (TLB shootdown, PTE management)
- [ ] SMP trampoline for additional CPUs
- [ ] User-mode GDT entries (ring-3 code/data) and TSS

## AArch64 Bring-up Plan (Phase 2)

- [x] EFI / QEMU virt boot entry (`aarch64/boot.S`)
- [x] PL011 UART for console output (`aarch64/platform.c`)
- [x] Cooperative context switch (`aarch64/context_switch.S`)
- [ ] MMU initialization (4-level translation tables)
- [ ] `pmap.c` — AArch64 physical map
- [ ] GIC interrupt controller
- [ ] SMP trampoline for additional CPUs

## References

- Intel® 64 and IA-32 Architectures SDM Vol. 3A — System Programming Guide (paging, GDT, interrupts)
- AMD64 Architecture Programmer's Manual Vol. 2 — System Programming (long mode, MSRs)
- ARM Architecture Reference Manual (ARMv8-A) — Exception Model, MMU
- AAPCS64 — Procedure Call Standard for the Arm 64-bit Architecture
- NS16550A UART Specification
- ARM PrimeCell UART (PL011) TRM (DDI 0183)
- GNU Mach `i386/` for x86 reference
- XNU `osfmk/x86_64/` and `osfmk/arm64/`
