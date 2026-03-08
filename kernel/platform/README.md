# kernel/platform/

Hardware Abstraction Layer (HAL) for NEOMACH kernel.

## Targets

| Directory   | Architecture | Priority |
|-------------|-------------|----------|
| `x86_64/`   | AMD64 / Intel 64 | Primary (Phase 1) |
| `aarch64/`  | ARM64 / Apple Silicon | Secondary (Phase 2+) |

## x86-64 Bring-up Plan

- [ ] Multiboot2 / UEFI boot entry point (`boot.S`)
- [ ] GDT / IDT setup
- [ ] Page table initialization (4-level paging)
- [ ] `pmap.c` — x86-64 physical map (TLB, PTE management)
- [ ] Interrupt and exception handlers
- [ ] APIC initialization (local APIC, IOAPIC)
- [ ] Serial UART driver (8250/16550) for console output
- [ ] SMP trampoline for additional CPUs

## AArch64 Bring-up Plan (Phase 2)

- [x] EFI / QEMU virt boot entry (`aarch64/boot.S`)
- [x] PL011 UART for console output (`aarch64/platform.c`)
- [x] Cooperative context switch (`aarch64/context_switch.S`)
- [ ] MMU initialization (4-level translation tables)
- [ ] `pmap.c` — AArch64 physical map
- [ ] GIC interrupt controller

## References

- Intel SDM Vol 3 (paging, interrupts)
- ARM Architecture Reference Manual (AArch64)
- GNU Mach `i386/` for x86 reference
- XNU `osfmk/x86_64/` and `osfmk/arm64/`
