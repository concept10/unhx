/*
 * kernel/kern/zalloc.c — Zone-based kernel memory allocator for UNHOX
 *
 * See zalloc.h for design rationale.
 *
 * Each zone manages a pool of fixed-size elements.  Elements are carved from
 * 4 KB pages obtained via vm_page_alloc().  Free elements are linked through
 * a pointer stored at the start of each free element.
 *
 * Zone sizes are powers of 2 from 32 to 4096 bytes.  Allocations larger
 * than 4096 bytes fall back to the static heap in kalloc.c.
 *
 * Reference: OSF MK kern/zalloc.c
 */

#include "zalloc.h"
#include "klib.h"
#include "vm/vm_page.h"

/* Page size (must match VM_PAGE_SIZE) */
#define PAGE_SIZE   4096

/*
 * Free list node — overlaid on the first sizeof(void*) bytes of each
 * free element.  The element must be at least 32 bytes, so this is safe.
 */
struct free_elem {
    struct free_elem *next;
};

/* The global zone array */
static struct zone zones[ZONE_MAX_ZONES];

/*
 * Zone size table: power-of-2 sizes from 32 to 4096.
 */
static const size_t zone_sizes[ZONE_MAX_ZONES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};

static const char *zone_names[ZONE_MAX_ZONES] = {
    "zone.32",   "zone.64",   "zone.128",  "zone.256",
    "zone.512",  "zone.1024", "zone.2048", "zone.4096"
};

/*
 * zone_expand — add one physical page to a zone, carving it into free elements.
 */
static int zone_expand(struct zone *z)
{
    struct vm_page *pg = vm_page_alloc();
    if (!pg)
        return -1;

    uint8_t *base = (uint8_t *)pg->pg_phys_addr;
    kmemset(base, 0, PAGE_SIZE);

    /* Carve the page into elements and push each onto the free list */
    size_t elems_per_page = PAGE_SIZE / z->z_elem_size;

    for (size_t i = 0; i < elems_per_page; i++) {
        struct free_elem *fe = (struct free_elem *)(base + i * z->z_elem_size);
        fe->next = (struct free_elem *)z->z_free_list;
        z->z_free_list = fe;
    }

    z->z_elem_count += elems_per_page;
    z->z_free_count += elems_per_page;
    z->z_page_count++;

    return 0;
}

void zalloc_init(void)
{
    for (int i = 0; i < ZONE_MAX_ZONES; i++) {
        zones[i].z_name       = zone_names[i];
        zones[i].z_elem_size  = zone_sizes[i];
        zones[i].z_elem_count = 0;
        zones[i].z_free_count = 0;
        zones[i].z_free_list  = (void *)0;
        zones[i].z_page_count = 0;
    }
}

struct zone *zone_lookup(size_t size)
{
    if (size == 0)
        return (void *)0;

    for (int i = 0; i < ZONE_MAX_ZONES; i++) {
        if (zones[i].z_elem_size >= size)
            return &zones[i];
    }

    /* Size exceeds largest zone — caller must handle separately */
    return (void *)0;
}

void *zalloc(struct zone *z)
{
    if (!z)
        return (void *)0;

    /* Expand zone if free list is empty */
    if (!z->z_free_list) {
        if (zone_expand(z) < 0)
            return (void *)0;
    }

    /* Pop from free list */
    struct free_elem *fe = (struct free_elem *)z->z_free_list;
    z->z_free_list = fe->next;
    z->z_free_count--;

    /* Zero the element before returning */
    kmemset(fe, 0, z->z_elem_size);

    return (void *)fe;
}

void zfree(struct zone *z, void *elem)
{
    if (!z || !elem)
        return;

    /* Push onto free list */
    struct free_elem *fe = (struct free_elem *)elem;
    fe->next = (struct free_elem *)z->z_free_list;
    z->z_free_list = fe;
    z->z_free_count++;
}
