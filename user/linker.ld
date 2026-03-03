/*
 * user/linker.ld — Linker script for UNHOX userspace programs
 *
 * Links the program to start at virtual address 0x400000.
 * The stack is set up by the kernel before entering _start.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

USER_BASE = 0x400000;

SECTIONS
{
    . = USER_BASE;

    .text ALIGN(4096) :
    {
        *(.text*)
    }

    .rodata ALIGN(4096) :
    {
        *(.rodata*)
    }

    .data ALIGN(4096) :
    {
        *(.data*)
    }

    .bss ALIGN(4096) :
    {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note.gnu.build-id)
    }
}
