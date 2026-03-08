/*
 * kernel/vm/vm.h — Virtual memory subsystem for NEOMACH
 *
 * The VM subsystem implements Mach's object-based virtual memory model.
 * Each task has a vm_map (its virtual address space) composed of vm_map_entry
 * ranges, each backed by a vm_object.  A vm_object may be anonymous (physical
 * RAM), or backed by an external pager (a userspace server that supplies pages
 * on demand — this is how filesystems work in Mach without ever entering the
 * kernel).
 *
 * Subsystem components (Phase 1 stubs; full implementation in Phase 3+):
 *   vm_page.h  / vm_page.c  — physical page frame allocator
 *   vm_map.h   / vm_map.c   — per-task virtual address spaces
 *   vm_object.h/ vm_object.c— backing memory objects (Phase 3)
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 */

#ifndef VM_H
#define VM_H

#include "mach/mach_types.h"

/*
 * vm_init — initialise the virtual memory subsystem.
 * Called during kernel startup after the Multiboot2 memory map is available.
 *
 * base and size describe the usable physical RAM range detected by the
 * bootloader.  vm_init passes this range to the physical page allocator.
 */
void vm_init(vm_address_t base, vm_size_t size);

#endif /* VM_H */
