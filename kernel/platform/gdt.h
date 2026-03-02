/*
 * kernel/platform/gdt.h — Global Descriptor Table for UNHOX (x86-64)
 *
 * The GDT is required by x86-64 even in long mode: the CPU uses segment
 * selectors to look up privilege level and type information.
 *
 * GDT layout (7 entries + TSS takes 2 slots = 9 8-byte entries total):
 *   Entry 0 — null descriptor (required by architecture)
 *   Entry 1 — kernel code segment (ring 0, 64-bit)
 *   Entry 2 — kernel data segment (ring 0)
 *   Entry 3 — user code segment   (ring 3, 64-bit)
 *   Entry 4 — user data segment   (ring 3)
 *   Entry 5 — TSS low  (system descriptor, 16 bytes total)
 *   Entry 6 — TSS high (upper 8 bytes of TSS descriptor)
 *
 * Note: sysret expects user CS = kernel CS + 16 and user SS = kernel CS + 8.
 * Our layout satisfies this for iretq (which doesn't have ordering constraints).
 *
 * Reference: Intel SDM Vol. 3A §3.4–§3.5, §7.2.3 (TSS in 64-bit mode).
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Segment selector constants
 *
 * A selector encodes the GDT index (bits 3–15), the TI flag (bit 2, 0 = GDT),
 * and the RPL (bits 0–1).
 * ------------------------------------------------------------------------- */

#define GDT_SELECTOR_NULL       0x00    /* null descriptor                   */
#define GDT_SELECTOR_CODE       0x08    /* kernel code (index 1, RPL=0)      */
#define GDT_SELECTOR_DATA       0x10    /* kernel data (index 2, RPL=0)      */
#define GDT_SELECTOR_UCODE      0x1B    /* user code   (index 3, RPL=3)      */
#define GDT_SELECTOR_UDATA      0x23    /* user data   (index 4, RPL=3)      */
#define GDT_SELECTOR_TSS        0x28    /* TSS         (index 5, RPL=0)      */

/* Number of 8-byte GDT entries (TSS occupies two: entries 5 and 6) */
#define GDT_ENTRY_COUNT     7

/* -------------------------------------------------------------------------
 * struct gdt_entry — one 8-byte GDT descriptor
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

/* Ring 0 segments */
#define GDT_ACCESS_CODE_RING0   0x9A    /* P=1, DPL=0, S=1, E=1, R=1        */
#define GDT_ACCESS_DATA_RING0   0x92    /* P=1, DPL=0, S=1, E=0, W=1        */

/* Ring 3 segments */
#define GDT_ACCESS_CODE_RING3   0xFA    /* P=1, DPL=3, S=1, E=1, R=1        */
#define GDT_ACCESS_DATA_RING3   0xF2    /* P=1, DPL=3, S=1, E=0, W=1        */

/* TSS descriptor access byte: P=1, DPL=0, type=0x9 (64-bit TSS, not busy) */
#define GDT_ACCESS_TSS          0x89

/*
 * Granularity byte flags
 */
#define GDT_GRAN_LONG_CODE      0xA0    /* G=1, L=1, D/B=0 (64-bit code)    */
#define GDT_GRAN_DATA           0xC0    /* G=1, D/B=1 (data)                 */

/* -------------------------------------------------------------------------
 * struct tss64 — 64-bit Task State Segment
 *
 * In long mode the TSS is used only for:
 *   - RSP0: kernel stack pointer loaded on ring 3 → ring 0 transition
 *   - IST1–IST7: interrupt stack pointers for specific IDT entries
 *   - I/O permission bitmap base (not used)
 *
 * Reference: Intel SDM Vol. 3A §7.7 — 64-Bit TSS.
 * ------------------------------------------------------------------------- */

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack for ring 3 → 0 transitions       */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;          /* interrupt stack table entries                  */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O permission bitmap offset                  */
} __attribute__((packed));

/* -------------------------------------------------------------------------
 * struct gdt_descriptor — the 10-byte GDTR value loaded by lgdt
 * ------------------------------------------------------------------------- */

struct gdt_descriptor {
    uint16_t limit;     /* size of GDT in bytes minus 1                     */
    uint64_t base;      /* linear address of the GDT                        */
} __attribute__((packed));

/*
 * gdt_init — build the GDT (with user segments and TSS) and load it.
 * Called once from platform_init() before any privilege transitions.
 */
void gdt_init(void);

/*
 * tss_set_rsp0 — update the TSS RSP0 field.
 * Called on every context switch to point RSP0 at the current thread's
 * kernel stack top, so ring 3 → ring 0 transitions land on the right stack.
 */
void tss_set_rsp0(uint64_t rsp0);

#endif /* GDT_H */
