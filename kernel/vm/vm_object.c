/*
 * kernel/vm/vm_object.c — Mach VM backing object lifecycle for UNHOX
 */

#include "vm_object.h"
#include "kern/kalloc.h"
#include "kern/klib.h"

struct vm_object *vm_object_create(vm_size_t size, uint32_t flags)
{
    struct vm_object *obj = (struct vm_object *)kalloc(sizeof(struct vm_object));
    if (!obj)
        return (void *)0;

    kmemset(obj, 0, sizeof(*obj));
    obj->ref_count = 1;
    obj->size = size;
    obj->flags = flags;
    obj->pager_port = MACH_PORT_NULL;
    return obj;
}

void vm_object_reference(struct vm_object *obj)
{
    if (!obj)
        return;
    obj->ref_count++;
}

void vm_object_deallocate(struct vm_object *obj)
{
    if (!obj)
        return;

    if (obj->ref_count == 0)
        return;

    obj->ref_count--;
    if (obj->ref_count != 0)
        return;

    /* Release resident physical pages before releasing the object itself. */
    for (uint32_t i = 0; i < VM_OBJECT_MAX_RESIDENT_PAGES; i++) {
        if (!obj->resident[i].in_use)
            continue;
        if (obj->resident[i].page)
            vm_page_free(obj->resident[i].page);
        obj->resident[i].page = (void *)0;
        obj->resident[i].in_use = 0;
    }

    kfree(obj);
}

void vm_object_set_pager(struct vm_object *obj, mach_port_t pager_port)
{
    if (!obj)
        return;
    obj->pager_port = pager_port;
    if (pager_port != MACH_PORT_NULL)
        obj->flags |= VM_OBJECT_PAGER;
}

struct vm_page *vm_object_resident_lookup(struct vm_object *obj, vm_offset_t offset)
{
    if (!obj)
        return (void *)0;

    offset = (vm_offset_t)VM_PAGE_TRUNC(offset);

    for (uint32_t i = 0; i < VM_OBJECT_MAX_RESIDENT_PAGES; i++) {
        if (!obj->resident[i].in_use)
            continue;
        if (obj->resident[i].offset == offset)
            return obj->resident[i].page;
    }

    return (void *)0;
}

kern_return_t vm_object_resident_insert(struct vm_object *obj,
                                        vm_offset_t offset,
                                        struct vm_page *page)
{
    if (!obj || !page)
        return KERN_INVALID_ARGUMENT;

    offset = (vm_offset_t)VM_PAGE_TRUNC(offset);

    for (uint32_t i = 0; i < VM_OBJECT_MAX_RESIDENT_PAGES; i++) {
        if (!obj->resident[i].in_use)
            continue;
        if (obj->resident[i].offset == offset) {
            obj->resident[i].page = page;
            return KERN_SUCCESS;
        }
    }

    for (uint32_t i = 0; i < VM_OBJECT_MAX_RESIDENT_PAGES; i++) {
        if (obj->resident[i].in_use)
            continue;
        obj->resident[i].in_use = 1;
        obj->resident[i].offset = offset;
        obj->resident[i].page = page;
        return KERN_SUCCESS;
    }

    return KERN_RESOURCE_SHORTAGE;
}
