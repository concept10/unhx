/*
 * kernel/platform/paging.c — x86-64 4-level paging for UNHOX
 *
 * Sets up the kernel's page tables with:
 *   - Identity mapping of the first 4 MB (needed while still executing from
 *     physical addresses during early boot)
 *   - Higher-half mapping at 0xFFFFFFFF80000000 for the kernel image
 *
 * Each level of the page table walk is documented inline.
 *
 * Reference: Intel SDM Vol. 3A §4.5 — 4-Level Paging;
 *            AMD64 APM Vol. 2 §5.3 — Long-Mode Page Translation.
 */

#include "paging.h"
#include "vm/vm_page.h"
#include "kern/klib.h"

/*
 * Boot page tables — statically allocated, page-aligned.
 *
 * We use static arrays for the initial kernel page tables because
 * vm_page_alloc() may not be ready yet when paging_init() runs.
 * Subsequent page table pages are allocated dynamically.
 */
static uint64_t kernel_pml4[PT_ENTRIES]  __attribute__((aligned(4096)));
static uint64_t kernel_pdpt[PT_ENTRIES]  __attribute__((aligned(4096)));
static uint64_t kernel_pd[PT_ENTRIES]    __attribute__((aligned(4096)));
static uint64_t identity_pdpt[PT_ENTRIES] __attribute__((aligned(4096)));
static uint64_t identity_pd[PT_ENTRIES]   __attribute__((aligned(4096)));

/* -------------------------------------------------------------------------
 * Helper: extract page table indices from a virtual address
 *
 * 48-bit virtual address layout (4-level paging):
 *   Bits [47:39] → PML4 index   (which PML4 entry)
 *   Bits [38:30] → PDPT index   (which PDPT entry)
 *   Bits [29:21] → PD index     (which PD entry)
 *   Bits [20:12] → PT index     (which PT entry)
 *   Bits [11:0]  → page offset  (byte within the 4 KB page)
 * ------------------------------------------------------------------------- */

static inline uint64_t pml4_index(uint64_t virt)
{
    return (virt >> 39) & 0x1FF;
}

static inline uint64_t pdpt_index(uint64_t virt)
{
    return (virt >> 30) & 0x1FF;
}

static inline uint64_t pd_index(uint64_t virt)
{
    return (virt >> 21) & 0x1FF;
}

static inline uint64_t pt_index(uint64_t virt)
{
    return (virt >> 12) & 0x1FF;
}

/* -------------------------------------------------------------------------
 * paging_map — map one 4 KB page: virt → phys with given flags.
 *
 * Walk the 4-level hierarchy, creating intermediate tables as needed.
 *
 * IMPORTANT: This function uses the page allocator (vm_page_alloc) for
 * intermediate tables.  Do not call it before vm_page_init().  For the
 * initial kernel mapping we use the statically allocated tables above.
 * ------------------------------------------------------------------------- */

void paging_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    /*
     * Level 4: PML4 → PDPT
     * kernel_pml4[pml4_index(virt)] should point to a PDPT.
     */
    uint64_t pml4i = pml4_index(virt);
    uint64_t *pdpt;

    if (kernel_pml4[pml4i] & PTE_PRESENT) {
        pdpt = (uint64_t *)(kernel_pml4[pml4i] & PTE_ADDR_MASK);
    } else {
        /* Allocate a new PDPT page */
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;  /* out of memory — silent fail for Phase 1 */
        pdpt = (uint64_t *)pg->pg_phys_addr;
        kmemset(pdpt, 0, 4096);
        kernel_pml4[pml4i] = (uint64_t)pdpt | PTE_PRESENT | PTE_WRITE;
    }

    /*
     * Level 3: PDPT → PD
     */
    uint64_t pdpti = pdpt_index(virt);
    uint64_t *pd;

    if (pdpt[pdpti] & PTE_PRESENT) {
        pd = (uint64_t *)(pdpt[pdpti] & PTE_ADDR_MASK);
    } else {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;
        pd = (uint64_t *)pg->pg_phys_addr;
        kmemset(pd, 0, 4096);
        pdpt[pdpti] = (uint64_t)pd | PTE_PRESENT | PTE_WRITE;
    }

    /*
     * Level 2: PD → PT
     */
    uint64_t pdi = pd_index(virt);
    uint64_t *pt;

    if (pd[pdi] & PTE_PRESENT) {
        /* Check if this is a 2 MB huge page — can't split it here */
        if (pd[pdi] & PTE_HUGE)
            return;  /* already mapped as huge page, skip */
        pt = (uint64_t *)(pd[pdi] & PTE_ADDR_MASK);
    } else {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;
        pt = (uint64_t *)pg->pg_phys_addr;
        kmemset(pt, 0, 4096);
        pd[pdi] = (uint64_t)pt | PTE_PRESENT | PTE_WRITE;
    }

    /*
     * Level 1: PT → physical page
     * This is the actual mapping.
     */
    uint64_t pti = pt_index(virt);
    pt[pti] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
}

/* -------------------------------------------------------------------------
 * paging_init — set up the kernel's initial page tables.
 *
 * We build a simple mapping using 2 MB huge pages for the identity map
 * and the higher-half kernel map.  This keeps the initial setup simple
 * and avoids needing the page allocator for intermediate PT pages.
 * ------------------------------------------------------------------------- */

void paging_init(uint64_t mmap_addr, uint32_t mmap_len)
{
    (void)mmap_addr;
    (void)mmap_len;

    /* Zero all static page tables */
    kmemset(kernel_pml4,   0, sizeof(kernel_pml4));
    kmemset(kernel_pdpt,   0, sizeof(kernel_pdpt));
    kmemset(kernel_pd,     0, sizeof(kernel_pd));
    kmemset(identity_pdpt, 0, sizeof(identity_pdpt));
    kmemset(identity_pd,   0, sizeof(identity_pd));

    /*
     * Identity map: virtual 0x00000000 → physical 0x00000000 (first 4 MB)
     *
     * PML4[0] → identity_pdpt
     * identity_pdpt[0] → identity_pd
     * identity_pd[0] → 0x00000000 (2 MB huge page)
     * identity_pd[1] → 0x00200000 (2 MB huge page)
     */
    identity_pd[0] = 0x00000000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    identity_pd[1] = 0x00200000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;

    identity_pdpt[0] = (uint64_t)identity_pd | PTE_PRESENT | PTE_WRITE;
    kernel_pml4[0]   = (uint64_t)identity_pdpt | PTE_PRESENT | PTE_WRITE;

    /*
     * Higher-half map: virtual 0xFFFFFFFF80000000 → physical 0x00000000
     *
     * PML4[511] → kernel_pdpt (PML4 index for 0xFFFFFFFF80000000 is 511)
     * kernel_pdpt[510] → kernel_pd (PDPT index for the address is 510)
     * kernel_pd[0] → 0x00000000 (2 MB huge page)
     * kernel_pd[1] → 0x00200000 (2 MB huge page)
     *
     * Index calculation:
     *   0xFFFFFFFF80000000 >> 39 = 0x1FF = 511 (PML4 index)
     *   0xFFFFFFFF80000000 >> 30 = 0x3FFFFFFFE → & 0x1FF = 510 (PDPT index)
     */
    kernel_pd[0] = 0x00000000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;
    kernel_pd[1] = 0x00200000ULL | PTE_PRESENT | PTE_WRITE | PTE_HUGE;

    kernel_pdpt[510] = (uint64_t)kernel_pd | PTE_PRESENT | PTE_WRITE;
    kernel_pml4[511] = (uint64_t)kernel_pdpt | PTE_PRESENT | PTE_WRITE;

    /*
     * Load CR3 with the new PML4 address.
     * This activates the new page tables.
     *
     * NOTE: In Phase 1, boot.S already sets up a minimal identity map and
     * we're running in long mode.  paging_init() replaces those boot tables
     * with a proper higher-half mapping.  The identity map at PML4[0] keeps
     * us from faulting during the transition.
     *
     * TODO (Phase 3): Remove the identity map after switching to higher-half
     * addresses.  For now it's harmless and simplifies debugging.
     */
    __asm__ volatile (
        "movq %0, %%cr3"
        :
        : "r"((uint64_t)kernel_pml4)
        : "memory"
    );
}
