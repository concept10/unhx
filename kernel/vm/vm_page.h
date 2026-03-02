/*
 * kernel/vm/vm_page.h — Physical page frame allocator for UNHOX
 *
 * Manages the pool of physical page frames using a free list (linked list
 * of free pages).  This is the lowest layer of the VM subsystem: it knows
 * about physical memory only, not virtual mappings.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 *            OSF MK vm/vm_page.h for the original structure definition.
 */

#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "mach/mach_types.h"
#include <stdint.h>

/*
 * struct vm_page — represents one physical page frame (4 KB on x86-64).
 *
 * When the page is free, pg_next links it into the global free list.
 * When the page is allocated, pg_next is unused (NULL).
 *
 * CMU Mach tracked additional per-page state (wire count, busy flag,
 * inactive/active LRU lists).  We defer those to Phase 3 when the
 * external pager protocol requires page replacement decisions.
 */
struct vm_page {
    struct vm_page *pg_next;         /* free list linkage                     */
    uint64_t        pg_phys_addr;    /* physical address of this page frame   */
    uint32_t        pg_flags;        /* page state flags (see below)          */
};

/* pg_flags values */
#define VM_PAGE_FREE        0x00
#define VM_PAGE_ALLOCATED   0x01
#define VM_PAGE_WIRED       0x02     /* TODO Phase 3: wired pages cannot be paged out */

/*
 * Maximum number of physical pages we track.
 * 512 pages = 2 MB.  Sufficient for Phase 1 kernel-only operation.
 * The Multiboot2 memory map may report more; we cap at this limit.
 *
 * TODO (Phase 2): Dynamically size based on actual physical memory.
 */
#define VM_PAGE_MAX     512

/* -------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

/*
 * vm_page_init — initialise the page allocator from a physical memory range.
 *
 * base: starting physical address of usable RAM
 * size: number of bytes of usable RAM
 *
 * Pages below base and above base+size are not tracked.
 * The kernel's own image is assumed to reside below base.
 */
void vm_page_init(uint64_t base, uint64_t size);

/*
 * vm_page_alloc — allocate one physical page frame.
 * Returns a pointer to the vm_page struct, or NULL if no free pages remain.
 */
struct vm_page *vm_page_alloc(void);

/*
 * vm_page_free — return a page frame to the free list.
 */
void vm_page_free(struct vm_page *page);

/*
 * vm_page_count_free — return the number of free pages (for diagnostics).
 */
uint32_t vm_page_count_free(void);

#endif /* VM_PAGE_H */
