/*
 * kernel/linker.ld — Linker script for the UNHOX higher-half kernel
 *
 * The kernel is linked to run at virtual address 0xFFFFFFFF80000000
 * (the "higher half" of the 64-bit virtual address space), but is loaded
 * by GRUB at physical address 0x100000 (1 MB, the traditional location
 * for Multiboot kernels above the legacy BIOS area).
 *
 * The difference between the virtual load address and the physical load
 * address is called the KERNEL_OFFSET:
 *   KERNEL_OFFSET = 0xFFFFFFFF80000000 - 0x0000000000100000
 *                 = 0xFFFFFFFF7FF00000
 *
 * During the boot.S trampoline the kernel executes from physical addresses
 * (the identity mapping covers the first 2 MB).  Once paging_init() sets up
 * the higher-half mapping, the kernel switches to virtual addresses and the
 * identity mapping can be removed.
 *
 * Reference: System V AMD64 ABI §3.3 — Process Initialization;
 *            "Higher Half Kernel" — OSDev Wiki.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Physical load address (where GRUB places the image) */
PHYS_BASE = 0x0000000000100000;

/* Virtual run address (where the kernel sees itself at runtime) */
VIRT_BASE = 0xFFFFFFFF80000000;

KERNEL_OFFSET = VIRT_BASE - PHYS_BASE;

SECTIONS
{
    /*
     * The Multiboot2 header must appear very early in the image so that GRUB
     * finds it in the first 32 KB scan.  We place it in its own section at
     * the start of the physical image, before the main text.
     */
    . = PHYS_BASE;

    .multiboot2 : AT(PHYS_BASE)
    {
        KEEP(*(.multiboot2))
    }

    /*
     * Main kernel text, mapped to the higher half.
     * AT(ADDR(.text) - KERNEL_OFFSET) tells the linker the physical load
     * address while ADDR() gives the virtual address.
     */
    . = VIRT_BASE + SIZEOF(.multiboot2);

    .text ALIGN(4096) : AT(ADDR(.text) - KERNEL_OFFSET)
    {
        *(.text.boot_paging)    /* boot page-table helpers first            */
        *(.text*)
    }

    .rodata ALIGN(4096) : AT(ADDR(.rodata) - KERNEL_OFFSET)
    {
        *(.rodata*)
    }

    .data ALIGN(4096) : AT(ADDR(.data) - KERNEL_OFFSET)
    {
        *(.data*)
    }

    /*
     * BSS: uninitialized data, zeroed by boot.S before calling kernel_main.
     * We export __bss_start and __bss_end so that the BSS-clearing loop in
     * boot.S can find the boundaries.
     */
    .bss ALIGN(4096) : AT(ADDR(.bss) - KERNEL_OFFSET)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* Discard debug / build-id sections we do not need in the final image */
    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note.gnu.build-id)
    }
}
