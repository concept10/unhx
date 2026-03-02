/*
 * kernel/platform/gdt.h — Global Descriptor Table for UNHU (x86-64)
 *
 * The GDT is required by x86-64 even in long mode: the CPU uses segment
 * selectors to look up privilege level and type information.  For a 64-bit
 * kernel we need only a minimal GDT:
 *
 *   Entry 0 — null descriptor (required by the architecture)
 *   Entry 1 — kernel code segment (ring 0, 64-bit)
 *   Entry 2 — kernel data segment (ring 0)
 *
 * User-mode segments (ring 3) and the TSS (Task State Segment) will be added
 * when we implement user tasks in Phase 2.
 *
 * Reference: Intel® 64 and IA-32 Architectures SDM Vol. 3A, §3.4 — Segment
 *            Descriptors; §3.4.5 — Segment Descriptor Types.
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Segment selector constants
 *
 * A selector encodes the GDT index (bits 3–15), the TI flag (bit 2, 0 = GDT),
 * and the RPL (bits 0–1, 0 = ring 0).
 * ------------------------------------------------------------------------- */

#define GDT_SELECTOR_NULL   0x00    /* null descriptor                       */
#define GDT_SELECTOR_CODE   0x08    /* kernel code segment (index 1, ring 0) */
#define GDT_SELECTOR_DATA   0x10    /* kernel data segment (index 2, ring 0) */

/* Number of GDT entries in the Phase 1 minimal GDT */
#define GDT_ENTRY_COUNT     3

/* -------------------------------------------------------------------------
 * struct gdt_entry — one 8-byte GDT descriptor
 *
 * The x86 descriptor format is notoriously fragmented for historical reasons.
 * Fields are split across non-contiguous bytes to maintain backward
 * compatibility with the 286 protected-mode format.
 *
 * In 64-bit long mode most base/limit fields are ignored for code and data
 * segments (the CPU treats them as flat); only the type and DPL bits matter.
 * ------------------------------------------------------------------------- */

struct gdt_entry {
    uint16_t limit_low;     /* bits 0–15 of segment limit                   */
    uint16_t base_low;      /* bits 0–15 of base address                    */
    uint8_t  base_middle;   /* bits 16–23 of base address                   */
    uint8_t  access;        /* present, DPL, type flags                     */
    uint8_t  granularity;   /* limit high (bits 16–19) + flags (G, D/B, L)  */
    uint8_t  base_high;     /* bits 24–31 of base address                   */
} __attribute__((packed));

/*
 * Access byte bit meanings:
 *   bit 7 — P     (Present): must be 1 for valid descriptors
 *   bit 6–5 — DPL : privilege level (0 = kernel, 3 = user)
 *   bit 4 — S     (Descriptor type): 1 = code/data, 0 = system
 *   bit 3 — E     (Executable): 1 = code segment, 0 = data
 *   bit 2 — DC    (Direction/Conforming)
 *   bit 1 — RW    (Read/Write enable)
 *   bit 0 — A     (Accessed, managed by CPU)
 */

/* Kernel code segment: present, ring 0, executable, readable */
#define GDT_ACCESS_CODE_RING0   0x9A
/* Kernel data segment: present, ring 0, read/write              */
#define GDT_ACCESS_DATA_RING0   0x92

/*
 * Granularity byte for 64-bit long-mode code segment:
 *   bit 7 — G  (Granularity): 1 = 4KB pages
 *   bit 6 — D/B: must be 0 for 64-bit code
 *   bit 5 — L  (Long mode): 1 = 64-bit code segment
 *   bit 4 — AVL: available for OS use
 *   bits 3–0 — limit high nibble
 */
#define GDT_GRAN_LONG_CODE      0xA0
#define GDT_GRAN_DATA           0xC0

/* -------------------------------------------------------------------------
 * struct gdt_descriptor — the 10-byte GDTR value loaded by lgdt
 * ------------------------------------------------------------------------- */

struct gdt_descriptor {
    uint16_t limit;     /* size of GDT in bytes minus 1                     */
    uint64_t base;      /* linear address of the GDT                        */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * gdt_init — build the GDT and load it with lgdt.
 * Called once from platform_init() before any privilege transitions.
 * ------------------------------------------------------------------------- */
void gdt_init(void);

#endif /* GDT_H */
