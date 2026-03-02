/*
 * kernel/kern/multiboot.h — Multiboot1 information structures for UNHOX
 *
 * Defines the Multiboot1 info structure passed by the bootloader in EBX.
 * We only use the module list (mods_count / mods_addr) to find user
 * programs passed via QEMU's -initrd flag.
 *
 * Reference: Multiboot Specification v0.6.96 §3.3.
 */

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <stdint.h>

/* Multiboot1 boot magic (in EAX on entry) */
#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

/* Multiboot1 info flags */
#define MULTIBOOT_INFO_MODS         (1 << 3)

/*
 * struct multiboot_mod — one Multiboot1 module entry.
 * The bootloader loads each -initrd file as a module.
 */
struct multiboot_mod {
    uint32_t    mod_start;   /* physical start address */
    uint32_t    mod_end;     /* physical end address (exclusive) */
    uint32_t    string;      /* physical address of command-line string */
    uint32_t    reserved;
};

/*
 * struct multiboot_info — Multiboot1 information structure.
 * Passed by the bootloader at the physical address in EBX.
 */
struct multiboot_info {
    uint32_t    flags;
    uint32_t    mem_lower;
    uint32_t    mem_upper;
    uint32_t    boot_device;
    uint32_t    cmdline;
    uint32_t    mods_count;
    uint32_t    mods_addr;
    /* ... remaining fields omitted (not needed) */
};

#endif /* MULTIBOOT_H */
