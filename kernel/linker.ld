/*
 * kernel/linker.ld — Linker script for the NEOMACH kernel (Phase 1)
 *
 * Phase 1: the kernel is linked and runs at physical address 0x100000 (1 MB).
 * This is the traditional location for Multiboot kernels above the legacy
 * BIOS area.  All addresses are physical — no higher-half mapping yet.
 *
 * Phase 2 will add a proper higher-half mapping (VIRT_BASE 0xFFFFFFFF80000000)
 * with a two-stage boot trampoline.
 *
 * Reference: Multiboot2 Specification §3.1 — OS image format.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHYS_BASE = 0x0000000000100000;

SECTIONS
{
    . = PHYS_BASE;

    /*
     * Multiboot1 header — must appear within the first 8 KB of the image.
     */
    .multiboot : AT(PHYS_BASE)
    {
        KEEP(*(.multiboot))
    }

    .text ALIGN(4096) : AT(ADDR(.text))
    {
        *(.text.boot_paging)
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
     * BSS: uninitialized data, zeroed by boot.S before calling kernel_main.
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
