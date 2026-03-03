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
#define MULTIBOOT_INFO_MEM_MAP      (1 << 6)

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
 * struct multiboot_mmap_entry — one memory map entry.
 *
 * type == 1 means usable RAM. Other types are reserved/firmware regions.
 */
struct multiboot_mmap_entry {
    uint32_t    size;       /* size of the entry excluding this field */
    uint64_t    addr;       /* base physical address of the region */
    uint64_t    len;        /* length in bytes */
    uint32_t    type;       /* 1 = available RAM */
} __attribute__((packed));

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

    /* a.out symbol table or ELF section header table (mutually exclusive) */
    uint32_t    syms0;
    uint32_t    syms1;
    uint32_t    syms2;
    uint32_t    syms3;

    uint32_t    mmap_length;
    uint32_t    mmap_addr;
};

#endif /* MULTIBOOT_H */
