/*
 * kernel/vm/vm_object.h — Mach VM backing object lifecycle for UNHOX
 *
 * A vm_object models the backing store for one or more virtual mappings.
 * In full Mach this can be anonymous memory, a file-backed pager, or a
 * device-backed pager. Phase 1 implements the core lifecycle and a small
 * resident-page cache used by vm_fault.
 */

#ifndef VM_OBJECT_H
#define VM_OBJECT_H

#include "mach/mach_types.h"
#include "vm_page.h"
#include <stdint.h>

/* Object kinds used by vm_fault and future pager integration. */
#define VM_OBJECT_ANON   0x0001U
#define VM_OBJECT_PAGER  0x0002U

/* Fixed-size resident page cache for early bring-up. */
#define VM_OBJECT_MAX_RESIDENT_PAGES  256

struct vm_object_page {
    vm_offset_t      offset;   /* byte offset into object, page-aligned */
    struct vm_page  *page;     /* resident physical page                 */
    int              in_use;   /* slot allocation flag                   */
};

struct vm_object {
    uint32_t                ref_count;
    vm_size_t               size;
    uint32_t                flags;
    mach_port_t             pager_port;
    struct vm_object_page   resident[VM_OBJECT_MAX_RESIDENT_PAGES];
};

/* Allocate a new vm_object with ref_count=1. */
struct vm_object *vm_object_create(vm_size_t size, uint32_t flags);

/* Increment/decrement references. Frees object at ref_count==0. */
void vm_object_reference(struct vm_object *obj);
void vm_object_deallocate(struct vm_object *obj);

/* Attach/detach a pager port for future external pager integration. */
void vm_object_set_pager(struct vm_object *obj, mach_port_t pager_port);

/* Resident page helpers used by vm_fault. */
struct vm_page *vm_object_resident_lookup(struct vm_object *obj, vm_offset_t offset);
kern_return_t vm_object_resident_insert(struct vm_object *obj,
                                        vm_offset_t offset,
                                        struct vm_page *page);

#endif /* VM_OBJECT_H */
