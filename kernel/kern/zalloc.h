/*
 * kernel/kern/zalloc.h — Zone-based kernel memory allocator for UNHOX
 *
 * Replaces the Phase 1 bump allocator with a zone allocator that supports
 * real kfree().  Follows the Mach zone_t pattern from OSF MK kern/zalloc.h.
 *
 * A "zone" is a pool of fixed-size elements.  Each zone is backed by one or
 * more physical pages obtained from vm_page_alloc().  Free elements within a
 * zone are linked into a per-zone free list.
 *
 * kalloc()/kfree() route allocations to the smallest zone whose element size
 * is >= the requested size.  Allocations larger than the largest zone fall
 * back to the static heap in kalloc.c.
 *
 * NOT thread-safe in Phase 2A.  Interrupt-safe locking added in Step 4
 * (preemptive scheduling).
 *
 * Reference: OSF MK kern/zalloc.h, Mach 3.0 zone allocator.
 */

#ifndef ZALLOC_H
#define ZALLOC_H

#include <stddef.h>
#include <stdint.h>

/*
 * Maximum number of zones.  We create zones for power-of-2 sizes from
 * ZONE_MIN_SIZE to ZONE_MAX_SIZE.
 */
#define ZONE_MIN_SIZE       32
#define ZONE_MAX_SIZE       4096
#define ZONE_MAX_ZONES      8       /* log2(4096/32) + 1 = 8 */

/*
 * struct zone — a pool of fixed-size elements.
 *
 * Elements are carved from pages.  Free elements are linked via a pointer
 * stored at the start of each free element (the element must be >= sizeof(void*)).
 */
struct zone {
    const char     *z_name;         /* descriptive name for debugging       */
    size_t          z_elem_size;    /* size of each element in bytes        */
    size_t          z_elem_count;   /* total elements across all pages      */
    size_t          z_free_count;   /* number of free elements              */
    void           *z_free_list;    /* singly-linked free list head         */
    uint32_t        z_page_count;   /* number of backing pages              */
};

/*
 * zalloc_init — initialise the zone allocator.
 *
 * Creates zones for sizes 32, 64, 128, 256, 512, 1024, 2048, 4096.
 * Must be called after vm_page_init() (needs physical pages).
 */
void zalloc_init(void);

/*
 * zone_lookup — find the zone for a given allocation size.
 *
 * Returns a pointer to the zone whose elem_size is the smallest >= size,
 * or NULL if size > ZONE_MAX_SIZE (caller must use page-level allocation).
 */
struct zone *zone_lookup(size_t size);

/*
 * zalloc — allocate one element from a zone.
 *
 * If the zone's free list is empty, a new page is added to the zone.
 * Returns NULL if no physical pages are available.
 * Memory is zeroed before return.
 */
void *zalloc(struct zone *z);

/*
 * zfree — return an element to its zone's free list.
 */
void zfree(struct zone *z, void *elem);

#endif /* ZALLOC_H */
