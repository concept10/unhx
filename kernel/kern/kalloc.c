/*
 * kernel/kern/kalloc.c — Bump allocator for UNHU Phase 1
 *
 * See kalloc.h for design rationale.
 */

#include "kalloc.h"
#include <stdint.h>

/* 16-byte alignment for all allocations (matches x86-64 ABI requirements) */
#define KALLOC_ALIGN    16
#define ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))

/* Static heap buffer */
static uint8_t  kheap[KHEAP_SIZE] __attribute__((aligned(KALLOC_ALIGN)));
static size_t   kheap_offset;

void kalloc_init(void)
{
    kheap_offset = 0;
}

void *kalloc(size_t size)
{
    if (size == 0)
        return (void *)0;

    size_t aligned_size = ALIGN_UP(size, KALLOC_ALIGN);
    size_t new_offset   = kheap_offset + aligned_size;

    if (new_offset > KHEAP_SIZE)
        return (void *)0;  /* out of memory */

    void *ptr = &kheap[kheap_offset];
    kheap_offset = new_offset;

    /* Zero the allocated memory (C convention for kernel allocators) */
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < aligned_size; i++)
        p[i] = 0;

    return ptr;
}

void kfree(void *ptr)
{
    /*
     * No-op in Phase 1.  The bump allocator never reclaims memory.
     * This is acceptable because Phase 1 allocates a small, bounded
     * number of objects during boot and never frees them.
     *
     * TODO (Phase 2): Implement zone-based allocation with real freeing.
     */
    (void)ptr;
}
