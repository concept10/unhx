/*
 * kernel/vm/vm.c — Virtual memory subsystem initialisation for NEOMACH
 *
 * Initialises the physical page allocator from the memory map provided by
 * the Multiboot2 bootloader, then sets up the kernel's own address space.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 */

#include "vm.h"
#include "vm_page.h"
#include "vm_map.h"
#include "kern/kalloc.h"

void vm_init(vm_address_t base, vm_size_t size)
{
    /*
     * Phase 1: Initialise the physical page allocator.
     *
     * The base/size parameters should come from the Multiboot2 memory map.
     * For Phase 1, if they are zero (boot.S doesn't parse the mmap yet),
     * we use a default range starting at 2 MB (above the kernel image)
     * covering 2 MB of physical RAM.  This gives us 512 pages to work with.
     */
    if (base == 0 && size == 0) {
        /* Default: physical memory from 2 MB to 4 MB */
        base = 0x200000;
        size = 0x200000;
    }

    vm_page_init((uint64_t)base, (uint64_t)size);

    /*
     * TODO (Phase 3):
     *   1. Set up the kernel's vm_map covering the higher-half mappings.
     *   2. Enable full 4-level x86-64 paging via paging_init().
     *   3. Remove the boot.S identity map.
     */
}

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
