/*
 * kernel/linker-aarch64.ld — Linker script for the NEOMACH kernel (AArch64)
 *
 * Targets QEMU's -machine virt board.  Physical RAM starts at 0x40000000;
 * the kernel is loaded there directly.
 *
 * Section layout:
 *   .text.boot   — boot stub (_start); must be first so ELF entry is correct
 *   .text         — kernel C and assembly code
 *   .rodata       — read-only data (string literals, const tables)
 *   .data         — initialised read-write data
 *   .bss          — uninitialised data (zeroed by boot.S before kernel_main)
 *
 * Reference: QEMU virt machine memory map; ARM ELF ABI specification.
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHYS_BASE = 0x40000000;

SECTIONS
{
    . = PHYS_BASE;

    /*
     * Boot stub — _start must land at the very beginning of the image so
     * that QEMU jumps to it when loading the ELF via -kernel.
     */
    .text.boot : AT(PHYS_BASE)
    {
        KEEP(*(.text.boot))
    }

    .text ALIGN(4096) : AT(ADDR(.text))
    {
        *(.text*)
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata))
    {
        *(.rodata*)
    }

    .data ALIGN(4096) : AT(ADDR(.data))
    {
        *(.data*)
    }

    /*
     * BSS: uninitialised data plus the initial kernel stack (defined in
     * boot.S).  boot.S zeroes this entire section before calling
     * kernel_main(), so the stack is clean at entry.
     */
    .bss ALIGN(4096) : AT(ADDR(.bss))
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note.gnu.build-id)
    }
}
