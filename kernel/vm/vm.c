/*
 * kernel/vm/vm.c — Virtual memory subsystem initialisation for UNHOX
 *
 * Initialises the physical page allocator from the memory map provided by
 * the Multiboot2 bootloader, then sets up the kernel's own address space.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 */

#include "vm.h"

void vm_init(vm_address_t base, vm_size_t size)
{
    (void)base;
    (void)size;
    /*
     * TODO (Phase 3):
     *   1. Pass (base, size) to vm_page_init() to populate the free page list.
     *   2. Set up the kernel's vm_map covering the higher-half mappings.
     *   3. Enable 4-level x86-64 paging via paging_init().
     */
}
