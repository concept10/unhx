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

/* -------------------------------------------------------------------------
 * paging_kernel_pml4_phys — return the physical address of the kernel PML4.
 * Since we use identity mapping, the pointer IS the physical address.
 * ------------------------------------------------------------------------- */

uint64_t paging_kernel_pml4_phys(void)
{
    return (uint64_t)kernel_pml4;
}

/* -------------------------------------------------------------------------
 * paging_create_task_pml4 — create a per-task PML4 for address space
 * isolation.  Copies the kernel's PML4[0] (identity) and PML4[511]
 * (higher-half) so that kernel code works in every task.
 * ------------------------------------------------------------------------- */

uint64_t paging_create_task_pml4(void)
{
    struct vm_page *pml4_pg = vm_page_alloc();
    if (!pml4_pg)
        return 0;

    uint64_t *pml4 = (uint64_t *)pml4_pg->pg_phys_addr;
    kmemset(pml4, 0, 4096);

    /*
     * Create a private PDPT for PML4[0].
     *
     * We cannot share the kernel's identity_pdpt directly, because
     * in x86-64 all page-table levels on the walk to a user page must
     * have PTE_USER set, and the kernel's identity entries do not.
     * Using the shared PDPT would also cause any new user page-table
     * entries to pollute the kernel's page tables.
     *
     * Instead, allocate a task-private PDPT with PTE_USER and a
     * task-private PD with PTE_USER, then copy only the two 2 MB
     * kernel huge-page entries (identity map, no PTE_USER — supervisor-
     * only) into the private PD.  User pages mapped later via
     * paging_map_page() will populate PD entries [2..511] with PTE_USER.
     */
    struct vm_page *pdpt_pg = vm_page_alloc();
    struct vm_page *pd_pg   = vm_page_alloc();
    if (!pdpt_pg || !pd_pg) {
        if (pdpt_pg) vm_page_free(pdpt_pg);
        if (pd_pg)   vm_page_free(pd_pg);
        vm_page_free(pml4_pg);
        return 0;
    }

    uint64_t *priv_pdpt = (uint64_t *)pdpt_pg->pg_phys_addr;
    uint64_t *priv_pd   = (uint64_t *)pd_pg->pg_phys_addr;
    kmemset(priv_pdpt, 0, 4096);
    kmemset(priv_pd,   0, 4096);

    /*
     * Copy the two 2 MB identity-map entries into the private PD.
     * These stay supervisor-only (no PTE_USER) so user code cannot
     * read the kernel's identity-mapped memory.
     */
    uint64_t *kern_pdpt = (uint64_t *)(kernel_pml4[0] & PTE_ADDR_MASK);
    uint64_t *kern_pd   = (uint64_t *)(kern_pdpt[0]   & PTE_ADDR_MASK);
    priv_pd[0] = kern_pd[0];   /* 0x000000 – 0x1FFFFF: supervisor only */
    priv_pd[1] = kern_pd[1];   /* 0x200000 – 0x3FFFFF: supervisor only */

    /* Wire PDPT[0] → private PD (PTE_USER so user walks can proceed) */
    priv_pdpt[0] = (uint64_t)priv_pd | PTE_PRESENT | PTE_WRITE | PTE_USER;

    /* Wire PML4[0] → private PDPT (PTE_USER) */
    pml4[0] = (uint64_t)priv_pdpt | PTE_PRESENT | PTE_WRITE | PTE_USER;

    /* Share the higher-half kernel mapping (read-only for user) */
    pml4[511] = kernel_pml4[511];

    return (uint64_t)pml4;
}

/* -------------------------------------------------------------------------
 * paging_map_page — map a 4 KB page in an arbitrary PML4.
 *
 * Walks the 4-level hierarchy from the given PML4, allocating
 * intermediate tables (PDPT, PD, PT) as needed.
 * ------------------------------------------------------------------------- */

void paging_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    if (!pml4)
        return;

    /* Level 4: PML4 → PDPT */
    uint64_t i4 = pml4_index(virt);
    uint64_t *pdpt;

    if (pml4[i4] & PTE_PRESENT) {
        pdpt = (uint64_t *)(pml4[i4] & PTE_ADDR_MASK);
    } else {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;
        pdpt = (uint64_t *)pg->pg_phys_addr;
        kmemset(pdpt, 0, 4096);
        pml4[i4] = (uint64_t)pdpt | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    /* Level 3: PDPT → PD */
    uint64_t i3 = pdpt_index(virt);
    uint64_t *pd;

    if (pdpt[i3] & PTE_PRESENT) {
        pd = (uint64_t *)(pdpt[i3] & PTE_ADDR_MASK);
    } else {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;
        pd = (uint64_t *)pg->pg_phys_addr;
        kmemset(pd, 0, 4096);
        pdpt[i3] = (uint64_t)pd | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    /* Level 2: PD → PT */
    uint64_t i2 = pd_index(virt);
    uint64_t *pt;

    if (pd[i2] & PTE_PRESENT) {
        if (pd[i2] & PTE_HUGE)
            return;  /* can't split a 2 MB huge page */
        pt = (uint64_t *)(pd[i2] & PTE_ADDR_MASK);
    } else {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) return;
        pt = (uint64_t *)pg->pg_phys_addr;
        kmemset(pt, 0, 4096);
        pd[i2] = (uint64_t)pt | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    /* Level 1: PT → physical page */
    uint64_t i1 = pt_index(virt);
    pt[i1] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;
}

/* -------------------------------------------------------------------------
 * paging_unmap_page — clear the PTE for a 4 KB page in an arbitrary PML4.
 * Does NOT free intermediate tables or the physical page.
 * ------------------------------------------------------------------------- */

void paging_unmap_page(uint64_t *pml4, uint64_t virt)
{
    if (!pml4)
        return;

    uint64_t i4 = pml4_index(virt);
    if (!(pml4[i4] & PTE_PRESENT)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[i4] & PTE_ADDR_MASK);

    uint64_t i3 = pdpt_index(virt);
    if (!(pdpt[i3] & PTE_PRESENT)) return;
    uint64_t *pd = (uint64_t *)(pdpt[i3] & PTE_ADDR_MASK);

    uint64_t i2 = pd_index(virt);
    if (!(pd[i2] & PTE_PRESENT) || (pd[i2] & PTE_HUGE)) return;
    uint64_t *pt = (uint64_t *)(pd[i2] & PTE_ADDR_MASK);

    uint64_t i1 = pt_index(virt);
    pt[i1] = 0;

    /* Invalidate TLB entry */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

/* -------------------------------------------------------------------------
 * paging_destroy_task_pml4 — free all per-task page table pages.
 *
 * Walks PML4 entries 1–510 (skipping kernel entries at 0 and 511) and
 * recursively frees PDPT → PD → PT pages.  Does NOT free physical
 * page frames that were mapped.
 * ------------------------------------------------------------------------- */

static void paging_free_pt_pages(uint64_t *pd)
{
    for (int i = 0; i < PT_ENTRIES; i++) {
        if ((pd[i] & PTE_PRESENT) && !(pd[i] & PTE_HUGE)) {
            uint64_t pt_phys = pd[i] & PTE_ADDR_MASK;
            struct vm_page *pg = vm_page_lookup(pt_phys);
            if (pg)
                vm_page_free(pg);
        }
    }
}

static void paging_free_pd_pages(uint64_t *pdpt)
{
    for (int i = 0; i < PT_ENTRIES; i++) {
        if (pdpt[i] & PTE_PRESENT) {
            uint64_t *pd = (uint64_t *)(pdpt[i] & PTE_ADDR_MASK);
            paging_free_pt_pages(pd);
            struct vm_page *pg = vm_page_lookup((uint64_t)pd);
            if (pg)
                vm_page_free(pg);
        }
    }
}

void paging_destroy_task_pml4(uint64_t pml4_phys)
{
    if (!pml4_phys)
        return;

    uint64_t *pml4 = (uint64_t *)pml4_phys;

    /*
     * Free all user-space entries: indices 0–510.
     *
     * PML4[0] was a task-private PDPT/PD (not the kernel's shared
     * identity_pdpt), so it must be freed.  paging_free_pd_pages uses
     * vm_page_lookup(), which returns NULL for kernel static arrays,
     * so kernel-only tables are safely skipped.
     * PML4[511] is the shared higher-half — skip it.
     */
    for (int i = 0; i < 511; i++) {
        if (pml4[i] & PTE_PRESENT) {
            uint64_t *pdpt = (uint64_t *)(pml4[i] & PTE_ADDR_MASK);
            paging_free_pd_pages(pdpt);
            struct vm_page *pg = vm_page_lookup((uint64_t)pdpt);
            if (pg)
                vm_page_free(pg);
        }
    }

    /* Free the PML4 page itself */
    struct vm_page *pg = vm_page_lookup(pml4_phys);
    if (pg)
        vm_page_free(pg);
}
