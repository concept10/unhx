/*
 * kernel/platform/platform.h — x86-64 hardware abstraction layer for UNHOX
 *
 * This subsystem provides the minimal hardware abstractions the kernel core
 * needs to start and run on x86-64 bare metal or under QEMU:
 *
 *   boot.S          — Multiboot2 header, GDT setup, stack, BSS clear,
 *                     jump to kernel_main()
 *   gdt.c / gdt.h   — Global Descriptor Table descriptors and lgdt wrapper
 *   paging.c        — 4-level x86-64 page table setup (Phase 3)
 *   serial.c        — NS16550-compatible UART for early console output
 *   interrupts.c    — IDT and basic interrupt handling (Phase 2)
 *
 * Nothing in this directory should be used above the kernel/kern layer.
 * Architecture-specific code never escapes the platform/ boundary.
 *
 * Reference: Intel® 64 and IA-32 Architectures Software Developer's Manual,
 *            Vol. 3A (System Programming Guide).
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include "mach/mach_types.h"

/*
 * platform_init — initialise all hardware abstractions.
 * Called early in kernel_main(), before any other subsystem.
 */
void platform_init(void);

/*
 * serial_putstr — write a NUL-terminated string to the debug serial port
 * (COM1, I/O port 0x3F8).  Safe to call before memory allocation is up.
 */
void serial_putstr(const char *s);

/*
 * serial_putchar — write a single character to COM1.
 */
void serial_putchar(char c);

/*
 * serial_puthex — write a 64-bit value as "0x" prefixed hexadecimal to COM1.
 */
void serial_puthex(uint64_t val);

/*
 * serial_putdec — write an unsigned 32-bit integer as decimal to COM1.
 */
void serial_putdec(uint32_t val);

#endif /* PLATFORM_H */
