/*
 * kernel/kern/kalloc.h — Kernel memory allocator for UNHOX
 *
 * Two-phase allocation:
 *   1. kalloc_init() — sets up the early-boot bump allocator (64 KB).
 *      Used for permanent kernel structures allocated before the VM is ready.
 *   2. kalloc_zones_init() — activates the zone allocator backed by
 *      vm_page_alloc() pages.  After this, kalloc() uses zones and
 *      kfree() actually reclaims memory.
 *
 * Reference: OSF MK kern/kalloc.c, kern/zalloc.h.
 */

#ifndef KALLOC_H
#define KALLOC_H

#include <stddef.h>

/*
 * kalloc_init — initialise the early-boot bump allocator.
 * Must be called before any kalloc() calls.
 */
void kalloc_init(void);

/*
 * kalloc_zones_init — activate the zone allocator.
 * Must be called after vm_page_init() (needs physical pages).
 * After this call, kalloc() allocates from zones and kfree() works.
 */
void kalloc_zones_init(void);

/*
 * kalloc — allocate size bytes of kernel memory.
 * Returns a pointer aligned to 16 bytes, or NULL if out of memory.
 * Memory is zeroed before return.
 */
void *kalloc(size_t size);

/*
 * kfree — free previously allocated kernel memory.
 * No-op for early-boot (bump) allocations.
 * For zone allocations, returns memory to the appropriate zone.
 */
void kfree(void *ptr);

#endif /* KALLOC_H */
