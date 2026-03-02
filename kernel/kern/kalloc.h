/*
 * kernel/kern/kalloc.h — Kernel memory allocator for UNHU (Phase 1)
 *
 * Phase 1 uses a simple bump allocator backed by a fixed-size static buffer.
 * This avoids the need for a full heap implementation while still allowing
 * dynamic allocation of kernel structures (ports, spaces, tasks, threads,
 * message buffers, etc.).
 *
 * kfree() is a no-op in Phase 1.  A proper slab/zone allocator (following
 * the Mach zone_t pattern from OSF MK kern/zalloc.h) is a Phase 2 task.
 *
 * NOTE: This allocator is NOT thread-safe.  Phase 1 runs single-threaded
 * until the scheduler is operational.
 */

#ifndef KALLOC_H
#define KALLOC_H

#include <stddef.h>

/*
 * KHEAP_SIZE — total kernel heap size.
 * 256 KB is generous for Phase 1 where we only allocate a handful of
 * ports, spaces, tasks, and message buffers.
 */
#define KHEAP_SIZE  (256 * 1024)

/*
 * kalloc_init — initialise the kernel heap.
 * Must be called before any kalloc() calls.
 */
void kalloc_init(void);

/*
 * kalloc — allocate size bytes of kernel memory.
 * Returns a pointer aligned to 16 bytes, or NULL if the heap is exhausted.
 */
void *kalloc(size_t size);

/*
 * kfree — free previously allocated kernel memory.
 * No-op in Phase 1 (bump allocator).
 *
 * TODO (Phase 2): Implement a Mach zone_t-style allocator with real
 *                 deallocation.  Reference: OSF MK kern/zalloc.h.
 */
void kfree(void *ptr);

#endif /* KALLOC_H */
