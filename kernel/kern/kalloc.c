/*
 * kernel/kern/kalloc.c — Kernel memory allocator for UNHOX
 *
 * Phase 2: Routes allocations through the zone allocator (zalloc) for
 * objects that fit within a single zone element.  Large allocations
 * (> ZONE_MAX_SIZE minus header overhead) fall back to the static heap.
 *
 * The static heap serves two roles:
 *   1. Early-boot allocations before vm_page_init() (bump region)
 *   2. Large allocations that exceed zone sizes (ipc_space, vm_map, stacks)
 *
 * kfree() works for zone-backed allocations.  Static-heap allocations
 * are not reclaimable (acceptable: they are long-lived kernel structures).
 *
 * Reference: OSF MK kern/kalloc.c
 */

#include "kalloc.h"
#include "zalloc.h"
#include <stdint.h>

/* 16-byte alignment for all allocations (matches x86-64 ABI requirements) */
#define KALLOC_ALIGN    16
#define ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))

/*
 * Static heap — bump allocator used for:
 *   - All allocations before zones are ready (early boot)
 *   - Large allocations that exceed zone sizes (after zones are ready)
 *
 * 256 KB is sufficient for Phase 2: the large objects are ipc_space (~4128),
 * vm_map (~3104), and thread stacks (8192).  With MAX_TASKS=16, worst case
 * is ~16 * (4128 + 3104 + 8192) ≈ 247 KB.
 */
#define STATIC_HEAP_SIZE  (256 * 1024)

static uint8_t  static_heap[STATIC_HEAP_SIZE] __attribute__((aligned(KALLOC_ALIGN)));
static size_t   static_heap_offset;
static int      zones_ready;

/*
 * For zone allocations, we prepend a small header to track the original
 * allocation size.  This lets kfree() find the right zone without the
 * caller passing the size.
 */
struct kalloc_header {
    size_t size;
    size_t _pad;  /* keep 16-byte alignment */
};

#define HEADER_SIZE     sizeof(struct kalloc_header)

static void *static_alloc(size_t size)
{
    size_t aligned_size = ALIGN_UP(size, KALLOC_ALIGN);
    size_t new_offset   = static_heap_offset + aligned_size;

    if (new_offset > STATIC_HEAP_SIZE)
        return (void *)0;

    void *ptr = &static_heap[static_heap_offset];
    static_heap_offset = new_offset;

    /* Zero the allocated memory */
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < aligned_size; i++)
        p[i] = 0;

    return ptr;
}

/* Check if a pointer falls within the static heap */
static int is_static_alloc(void *ptr)
{
    uint8_t *p = (uint8_t *)ptr;
    return (p >= static_heap && p < static_heap + STATIC_HEAP_SIZE);
}

void kalloc_init(void)
{
    static_heap_offset = 0;
    zones_ready = 0;
}

void kalloc_zones_init(void)
{
    zalloc_init();
    zones_ready = 1;
}

void *kalloc(size_t size)
{
    if (size == 0)
        return (void *)0;

    /* Before zones are ready, use the static allocator */
    if (!zones_ready)
        return static_alloc(size);

    /* Check if this fits in a zone (with header overhead) */
    size_t total = size + HEADER_SIZE;
    struct zone *z = zone_lookup(total);

    if (z) {
        /* Zone allocation */
        void *raw = zalloc(z);
        if (!raw)
            return (void *)0;

        struct kalloc_header *hdr = (struct kalloc_header *)raw;
        hdr->size = size;
        return (void *)((uint8_t *)raw + HEADER_SIZE);
    }

    /* Large allocation — fall back to static heap (not zone-backed) */
    return static_alloc(size);
}

void kfree(void *ptr)
{
    if (!ptr)
        return;

    /* Static-heap allocations cannot be freed (bump allocator) */
    if (is_static_alloc(ptr))
        return;

    /* Zone allocation: recover the header to find the zone */
    struct kalloc_header *hdr =
        (struct kalloc_header *)((uint8_t *)ptr - HEADER_SIZE);
    size_t total = hdr->size + HEADER_SIZE;

    struct zone *z = zone_lookup(total);
    if (z)
        zfree(z, hdr);
}
