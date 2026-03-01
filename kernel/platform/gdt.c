/*
 * kernel/platform/gdt.c — GDT initialisation for UNHOX (x86-64)
 *
 * Builds a minimal three-entry Global Descriptor Table and loads it via the
 * lgdt instruction.  After gdt_init() returns all segment registers hold
 * valid selectors and the CPU is in a well-defined long-mode state.
 *
 * Reference: Intel® 64 and IA-32 Architectures SDM Vol. 3A §3.4–§3.5.
 */

#include "gdt.h"
#include <stddef.h>

/* The GDT table itself — statically allocated, aligned for hardware access */
static struct gdt_entry  gdt_table[GDT_ENTRY_COUNT] __attribute__((aligned(8)));
static struct gdt_descriptor gdt_desc;

/* -------------------------------------------------------------------------
 * gdt_set_entry — helper to fill one GDT slot
 * ------------------------------------------------------------------------- */

static void gdt_set_entry(int index,
                           uint32_t base,
                           uint32_t limit,
                           uint8_t  access,
                           uint8_t  granularity)
{
    struct gdt_entry *e = &gdt_table[index];

    e->base_low    = (uint16_t)(base & 0xFFFF);
    e->base_middle = (uint8_t)((base >> 16) & 0xFF);
    e->base_high   = (uint8_t)((base >> 24) & 0xFF);

    e->limit_low   = (uint16_t)(limit & 0xFFFF);
    e->granularity = (uint8_t)((granularity & 0xF0) | ((limit >> 16) & 0x0F));

    e->access      = access;
}

/* -------------------------------------------------------------------------
 * gdt_load — load the GDTR and reload segment registers
 *
 * After lgdt we must reload CS via a far jump (or, in 64-bit mode, a far
 * return) because changing the GDT does not automatically update the
 * segment descriptor cache.
 * ------------------------------------------------------------------------- */

static void gdt_load(void)
{
    __asm__ volatile (
        "lgdt %0\n\t"
        /*
         * Reload CS with the kernel code selector via a far return.
         * We push the new CS value and the return address, then lret.
         * This is the standard technique for reloading CS in 64-bit mode
         * because a direct far jump with a 64-bit target is not encodable.
         */
        "pushq %1\n\t"              /* push kernel code selector             */
        "leaq  1f(%%rip), %%rax\n\t"/* load address of label '1'            */
        "pushq %%rax\n\t"           /* push return address                   */
        "lretq\n\t"                 /* far return: loads CS, jumps to label  */
        "1:\n\t"
        /* Reload all data segment registers with the kernel data selector */
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        :
        : "m"(gdt_desc),
          "i"((uint64_t)GDT_SELECTOR_CODE),
          "i"((uint16_t)GDT_SELECTOR_DATA)
        : "rax", "memory"
    );
}

/* -------------------------------------------------------------------------
 * gdt_init — public entry point
 * ------------------------------------------------------------------------- */

void gdt_init(void)
{
    /* Entry 0: null descriptor — required; all fields must be zero */
    gdt_set_entry(0, 0, 0, 0, 0);

    /*
     * Entry 1: kernel code segment
     * base=0, limit=0xFFFFF (with G=1 → 4GB, ignored in 64-bit mode)
     * access = present | ring-0 | code | readable
     * gran   = 4KB granularity | L=1 (64-bit) | D/B=0
     */
    gdt_set_entry(1, 0, 0xFFFFF, GDT_ACCESS_CODE_RING0, GDT_GRAN_LONG_CODE);

    /*
     * Entry 2: kernel data segment
     * base=0, limit=0xFFFFF
     * access = present | ring-0 | data | read-write
     * gran   = 4KB granularity | 32-bit (D/B=1); data segs ignore L bit
     */
    gdt_set_entry(2, 0, 0xFFFFF, GDT_ACCESS_DATA_RING0, GDT_GRAN_DATA);

    /* Set up the GDTR descriptor */
    gdt_desc.limit = (uint16_t)(sizeof(gdt_table) - 1);
    gdt_desc.base  = (uint64_t)(uintptr_t)gdt_table;

    gdt_load();
}
