/*
 * kernel/vm/vm_map.h — Task virtual address space for UNHOX
 *
 * In Mach, each task has a vm_map representing its virtual address space.
 * The vm_map is an ordered collection of vm_map_entry ranges, each describing
 * a contiguous region of virtual addresses backed by a vm_object.
 *
 * A vm_object represents a source of data for pages:
 *   - Anonymous memory (physical pages, no backing store)
 *   - File-backed memory (supplied by an external pager — a userspace server)
 *   - Device memory (supplied by a device pager)
 *
 * The external pager protocol is Mach's mechanism for keeping the kernel
 * ignorant of filesystems: the kernel asks a pager for a page via IPC,
 * the pager replies with data, and the kernel maps it.
 *
 * For Phase 1 we define the structures but defer full implementation.
 * The kernel task uses a trivial identity mapping set up in boot.S.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §5 — Virtual Memory.
 *
 * TODO (Phase 3): Implement vm_allocate(), vm_deallocate(), vm_map(),
 *                 vm_protect(), and the external pager protocol.
 */

#ifndef VM_MAP_H
#define VM_MAP_H

#include "mach/mach_types.h"
#include <stdint.h>

/* Forward declaration */
struct vm_object;

/* Maximum entries per vm_map in Phase 1 */
#define VM_MAP_MAX_ENTRIES  64

/*
 * struct vm_map_entry — one contiguous virtual address range in a vm_map.
 *
 * CMU Mach 3.0 paper §5.2: "Each entry describes a region with a starting
 * address, a size, protection attributes, and a reference to a memory object."
 */
struct vm_map_entry {
    vm_address_t    vme_start;       /* inclusive start of virtual range      */
    vm_address_t    vme_end;         /* exclusive end of virtual range        */
    vm_prot_t       vme_protection;  /* current protection                   */
    vm_prot_t       vme_max_protection; /* maximum allowed protection         */
    struct vm_object *vme_object;    /* backing memory object                 */
    vm_offset_t     vme_offset;      /* offset into the object                */
    int             vme_in_use;      /* 1 if this entry slot is occupied      */
};

/*
 * struct vm_map — a task's virtual address space.
 *
 * CMU Mach 3.0 paper §5.1: "The address space of a task is described by a
 * vm_map, which is an ordered list of vm_map_entry records."
 *
 * Phase 1 uses a flat array; real Mach uses a red-black tree for O(log n)
 * lookup.  Deferred to Phase 3.
 */
struct vm_map {
    struct vm_map_entry entries[VM_MAP_MAX_ENTRIES];
    uint32_t            entry_count;     /* number of entries in use          */
    vm_address_t        min_offset;      /* lowest allocatable address        */
    vm_address_t        max_offset;      /* highest allocatable address       */
    uint64_t           *pml4;            /* x86-64: pointer to PML4 table     */
};

/*
 * vm_map_create — allocate and initialise a new empty vm_map.
 * Returns NULL on allocation failure.
 */
struct vm_map *vm_map_create(vm_address_t min, vm_address_t max);

/*
 * vm_map_enter — allocate a region in the address space.
 *
 * Finds a free slot, allocates physical pages, and maps them into the
 * task's page tables via paging_map_page().
 *
 * map:  the vm_map to modify
 * addr: desired virtual address (must be page-aligned)
 * size: size in bytes (rounded up to page boundary)
 * prot: protection flags (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)
 *
 * Returns KERN_SUCCESS on success, or an error code.
 */
kern_return_t vm_map_enter(struct vm_map *map,
                           vm_address_t addr,
                           vm_size_t size,
                           vm_prot_t prot);

/*
 * vm_map_remove — remove a region from the address space.
 *
 * Unmaps pages, frees the physical page frames, and removes the
 * vm_map_entry.
 *
 * Returns KERN_SUCCESS if the region was found and removed.
 */
kern_return_t vm_map_remove(struct vm_map *map,
                            vm_address_t start,
                            vm_address_t end);

/*
 * vm_map_lookup — find the vm_map_entry containing the given address.
 * Returns NULL if the address is not mapped.
 */
struct vm_map_entry *vm_map_lookup(struct vm_map *map, vm_address_t addr);

#endif /* VM_MAP_H */
