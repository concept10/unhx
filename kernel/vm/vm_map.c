/*
 * kernel/vm/vm_map.c — Task virtual address space operations for UNHOX
 *
 * Implements vm_map_enter() and vm_map_remove(): the kernel operations
 * that allocate and free regions of a task's virtual address space.
 *
 * Each vm_map_entry describes a contiguous virtual range backed by
 * physical pages.  vm_map_enter() allocates physical pages and maps
 * them into the task's page tables; vm_map_remove() unmaps and frees them.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 */

#include "vm_map.h"
#include "vm_page.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "platform/paging.h"

/*
 * vm_map_create — allocate and initialise a new empty vm_map.
 */
struct vm_map *vm_map_create(vm_address_t min, vm_address_t max)
{
    struct vm_map *map = (struct vm_map *)kalloc(sizeof(struct vm_map));
    if (!map)
        return (void *)0;

    for (uint32_t i = 0; i < VM_MAP_MAX_ENTRIES; i++)
        map->entries[i].vme_in_use = 0;

    map->entry_count = 0;
    map->min_offset  = min;
    map->max_offset  = max;
    map->pml4        = (void *)0;

    return map;
}

/*
 * vm_map_lookup — find the entry containing 'addr'.
 */
struct vm_map_entry *vm_map_lookup(struct vm_map *map, vm_address_t addr)
{
    if (!map)
        return (void *)0;

    for (uint32_t i = 0; i < VM_MAP_MAX_ENTRIES; i++) {
        struct vm_map_entry *e = &map->entries[i];
        if (e->vme_in_use && addr >= e->vme_start && addr < e->vme_end)
            return e;
    }
    return (void *)0;
}

/*
 * Check whether a proposed [start, end) range overlaps any existing entry.
 */
static int vm_map_overlaps(struct vm_map *map, vm_address_t start, vm_address_t end)
{
    for (uint32_t i = 0; i < VM_MAP_MAX_ENTRIES; i++) {
        struct vm_map_entry *e = &map->entries[i];
        if (e->vme_in_use && start < e->vme_end && end > e->vme_start)
            return 1;
    }
    return 0;
}

/*
 * Find a free entry slot.
 */
static struct vm_map_entry *vm_map_find_free_entry(struct vm_map *map)
{
    for (uint32_t i = 0; i < VM_MAP_MAX_ENTRIES; i++) {
        if (!map->entries[i].vme_in_use)
            return &map->entries[i];
    }
    return (void *)0;
}

/*
 * Convert VM_PROT_* to PTE_* flags.
 */
static uint64_t vm_prot_to_pte(vm_prot_t prot)
{
    uint64_t flags = PTE_PRESENT | PTE_USER;

    if (prot & VM_PROT_WRITE)
        flags |= PTE_WRITE;

    /* NX bit: if not executable and CPU supports NX, set it.
     * For simplicity in Phase 2, we skip NX. */

    return flags;
}

/*
 * vm_map_enter — allocate and map a region of virtual address space.
 *
 * Allocates physical pages for the region and maps them into the
 * task's page tables.
 */
kern_return_t vm_map_enter(struct vm_map *map,
                           vm_address_t addr,
                           vm_size_t size,
                           vm_prot_t prot)
{
    if (!map || !map->pml4)
        return KERN_INVALID_ARGUMENT;

    /* Page-align */
    addr = VM_PAGE_TRUNC(addr);
    size = VM_PAGE_ROUND(size);

    if (size == 0)
        return KERN_INVALID_ARGUMENT;

    vm_address_t end = addr + size;

    /* Range check */
    if (addr < map->min_offset || end > map->max_offset)
        return KERN_INVALID_ADDRESS;

    /* Overlap check */
    if (vm_map_overlaps(map, addr, end))
        return KERN_NO_SPACE;

    /* Find a free entry slot */
    struct vm_map_entry *entry = vm_map_find_free_entry(map);
    if (!entry)
        return KERN_RESOURCE_SHORTAGE;

    /* Allocate physical pages and map them */
    uint64_t pte_flags = vm_prot_to_pte(prot);
    vm_address_t va = addr;

    while (va < end) {
        struct vm_page *pg = vm_page_alloc();
        if (!pg) {
            /* Out of memory — unmap what we already mapped */
            vm_address_t undo = addr;
            while (undo < va) {
                paging_unmap_page(map->pml4, undo);
                undo += VM_PAGE_SIZE;
            }
            return KERN_RESOURCE_SHORTAGE;
        }

        /* Zero the page (important for user memory) */
        kmemset((void *)pg->pg_phys_addr, 0, VM_PAGE_SIZE);

        paging_map_page(map->pml4, va, pg->pg_phys_addr, pte_flags);
        va += VM_PAGE_SIZE;
    }

    /* Record the mapping */
    entry->vme_start          = addr;
    entry->vme_end            = end;
    entry->vme_protection     = prot;
    entry->vme_max_protection = VM_PROT_ALL;
    entry->vme_object         = (void *)0;
    entry->vme_offset         = 0;
    entry->vme_in_use         = 1;
    map->entry_count++;

    return KERN_SUCCESS;
}

/*
 * vm_map_remove — unmap a region and free its physical pages.
 */
kern_return_t vm_map_remove(struct vm_map *map,
                            vm_address_t start,
                            vm_address_t end)
{
    if (!map || !map->pml4)
        return KERN_INVALID_ARGUMENT;

    start = VM_PAGE_TRUNC(start);
    end   = VM_PAGE_ROUND(end);

    /* Find the matching entry */
    for (uint32_t i = 0; i < VM_MAP_MAX_ENTRIES; i++) {
        struct vm_map_entry *e = &map->entries[i];
        if (!e->vme_in_use)
            continue;
        if (e->vme_start != start || e->vme_end != end)
            continue;

        /* Unmap each page and free the physical frame */
        for (vm_address_t va = start; va < end; va += VM_PAGE_SIZE) {
            /* Walk page tables to find the physical page */
            /* For now, we just unmap; a full implementation would
             * walk the PTE to find and free the physical page. */
            paging_unmap_page(map->pml4, va);
        }

        e->vme_in_use = 0;
        map->entry_count--;
        return KERN_SUCCESS;
    }

    return KERN_INVALID_ADDRESS;
}
