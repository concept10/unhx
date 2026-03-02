/*
 * kernel/platform/gdt.c — GDT initialisation for UNHOX (x86-64)
 *
 * Builds a 7-entry GDT with kernel/user segments and a TSS descriptor,
 * then loads it via lgdt and loads the TR (task register) via ltr.
 *
 * GDT layout:
 *   [0] null    [1] kcode    [2] kdata    [3] ucode    [4] udata
 *   [5] TSS low [6] TSS high
 *
 * Reference: Intel SDM Vol. 3A §3.4–§3.5, §7.2.3 (TSS in 64-bit mode).
 */

#include "gdt.h"
#include "kern/klib.h"

/* The GDT table itself — statically allocated, aligned for hardware access */
static struct gdt_entry  gdt_table[GDT_ENTRY_COUNT] __attribute__((aligned(8)));
static struct gdt_descriptor gdt_desc;

/* The single TSS (one CPU, no SMP) */
static struct tss64 tss __attribute__((aligned(16)));

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

/*
 * Install the 16-byte TSS descriptor in GDT entries 5 and 6.
 *
 * In 64-bit mode, system segment descriptors (TSS, LDT) are 16 bytes wide.
 * They occupy two consecutive GDT slots:
 *   Slot 5: standard 8-byte descriptor with base[31:0] and limit
 *   Slot 6: upper 8 bytes contain base[63:32] and reserved fields
 */
static void gdt_set_tss(uint64_t base, uint32_t limit)
{
    /* Low 8 bytes (GDT entry 5) */
    gdt_table[5].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt_table[5].base_low    = (uint16_t)(base & 0xFFFF);
    gdt_table[5].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt_table[5].access      = GDT_ACCESS_TSS;
    gdt_table[5].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt_table[5].base_high   = (uint8_t)((base >> 24) & 0xFF);

    /* High 8 bytes (GDT entry 6): base[63:32] and reserved */
    uint32_t *high = (uint32_t *)&gdt_table[6];
    high[0] = (uint32_t)(base >> 32);   /* base [63:32] */
    high[1] = 0;                         /* reserved      */
}

/* -------------------------------------------------------------------------
 * gdt_load — load the GDTR and reload segment registers
 * ------------------------------------------------------------------------- */

static void gdt_load(void)
{
    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq %1\n\t"
        "leaq  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
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

/* Load the Task Register with the TSS selector */
static void tss_load(void)
{
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_SELECTOR_TSS) : "memory");
}

/* -------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

void gdt_init(void)
{
    /* Entry 0: null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code segment (ring 0, 64-bit) */
    gdt_set_entry(1, 0, 0xFFFFF, GDT_ACCESS_CODE_RING0, GDT_GRAN_LONG_CODE);

    /* Entry 2: kernel data segment (ring 0) */
    gdt_set_entry(2, 0, 0xFFFFF, GDT_ACCESS_DATA_RING0, GDT_GRAN_DATA);

    /* Entry 3: user code segment (ring 3, 64-bit) */
    gdt_set_entry(3, 0, 0xFFFFF, GDT_ACCESS_CODE_RING3, GDT_GRAN_LONG_CODE);

    /* Entry 4: user data segment (ring 3) */
    gdt_set_entry(4, 0, 0xFFFFF, GDT_ACCESS_DATA_RING3, GDT_GRAN_DATA);

    /* Initialise the TSS */
    kmemset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);  /* no I/O permission bitmap */

    /* Entries 5–6: TSS descriptor (16 bytes spanning two GDT slots) */
    gdt_set_tss((uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);

    /* Set up the GDTR descriptor */
    gdt_desc.limit = (uint16_t)(sizeof(gdt_table) - 1);
    gdt_desc.base  = (uint64_t)(uintptr_t)gdt_table;

    /* Load the GDT and TSS */
    gdt_load();
    tss_load();
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp0 = rsp0;
}
