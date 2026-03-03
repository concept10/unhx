/*
 * kernel/vm/vm_fault.c — Page fault handling for UNHOX
 */

#include "vm_fault.h"
#include "vm_map.h"
#include "vm_object.h"
#include "vm_page.h"
#include "kern/sched.h"
#include "kern/thread.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "platform/paging.h"
#include "platform/idt.h"

/* x86-64 page-fault error bits (Intel SDM Vol. 3A, #PF). */
#define PF_ERR_PRESENT   (1ULL << 0)  /* 1 = protection violation, 0 = not-present */
#define PF_ERR_WRITE     (1ULL << 1)  /* 1 = write access, 0 = read access           */
#define PF_ERR_USER      (1ULL << 2)  /* 1 = fault from CPL3                           */
#define PF_ERR_IFETCH    (1ULL << 4)  /* 1 = instruction fetch                         */

/* Reject canonical-kernel addresses in the lower-half user fault path. */
#define KERNEL_VA_BASE   0xFFFF800000000000ULL

static uint64_t pml4_index(uint64_t virt) { return (virt >> 39) & 0x1FF; }
static uint64_t pdpt_index(uint64_t virt) { return (virt >> 30) & 0x1FF; }
static uint64_t pd_index(uint64_t virt)   { return (virt >> 21) & 0x1FF; }
static uint64_t pt_index(uint64_t virt)   { return (virt >> 12) & 0x1FF; }

static int vm_is_page_present(uint64_t *pml4, uint64_t va)
{
    if (!pml4)
        return 0;

    uint64_t e4 = pml4[pml4_index(va)];
    if (!(e4 & PTE_PRESENT))
        return 0;

    uint64_t *pdpt = (uint64_t *)(e4 & PTE_ADDR_MASK);
    uint64_t e3 = pdpt[pdpt_index(va)];
    if (!(e3 & PTE_PRESENT))
        return 0;

    uint64_t *pd = (uint64_t *)(e3 & PTE_ADDR_MASK);
    uint64_t e2 = pd[pd_index(va)];
    if (!(e2 & PTE_PRESENT))
        return 0;
    if (e2 & PTE_HUGE)
        return 1;

    uint64_t *pt = (uint64_t *)(e2 & PTE_ADDR_MASK);
    uint64_t e1 = pt[pt_index(va)];
    return (e1 & PTE_PRESENT) ? 1 : 0;
}

static uint64_t vm_prot_to_pte(vm_prot_t prot)
{
    uint64_t flags = PTE_PRESENT | PTE_USER;

    if (prot & VM_PROT_WRITE)
        flags |= PTE_WRITE;

    return flags;
}

static vm_prot_t fault_access_to_prot(uint64_t error_code)
{
    vm_prot_t need = VM_PROT_READ;

    if (error_code & PF_ERR_WRITE)
        need = VM_PROT_WRITE;
    if (error_code & PF_ERR_IFETCH)
        need = VM_PROT_EXECUTE;

    return need;
}

kern_return_t vm_fault_handle(struct interrupt_frame *frame,
                              uint64_t fault_addr,
                              uint64_t error_code)
{
    (void)frame;

    /* Phase 1: recover only user-mode not-present faults. */
    if ((error_code & PF_ERR_USER) == 0)
        return KERN_FAILURE;

    struct thread *th = sched_current();
    if (!th || !th->th_task || !th->th_task->t_map)
        return KERN_FAILURE;

    struct vm_map *map = th->th_task->t_map;
    if (!map->pml4)
        return KERN_FAILURE;

    /* Keep kernel faults on the existing panic path for now. */
    if (fault_addr >= KERNEL_VA_BASE)
        return KERN_FAILURE;

    struct vm_map_entry *entry = vm_map_lookup(map, (vm_address_t)fault_addr);
    if (!entry)
        return KERN_INVALID_ADDRESS;

    vm_prot_t required = fault_access_to_prot(error_code);
    if ((entry->vme_protection & required) == 0)
        return KERN_PROTECTION_FAILURE;

    /* Protection faults on present pages are not recoverable in Phase 1. */
    if (error_code & PF_ERR_PRESENT)
        return KERN_PROTECTION_FAILURE;

    vm_address_t va = VM_PAGE_TRUNC((vm_address_t)fault_addr);
    if (vm_is_page_present(map->pml4, va))
        return KERN_PROTECTION_FAILURE;

    struct vm_page *pg = (void *)0;
    if (entry->vme_object) {
        vm_offset_t off = entry->vme_offset + (vm_offset_t)(va - entry->vme_start);
        pg = vm_object_resident_lookup(entry->vme_object, off);
    }

    if (!pg) {
        pg = vm_page_alloc();
        if (!pg)
            return KERN_RESOURCE_SHORTAGE;

        kmemset((void *)pg->pg_phys_addr, 0, VM_PAGE_SIZE);

        if (entry->vme_object) {
            vm_offset_t off = entry->vme_offset + (vm_offset_t)(va - entry->vme_start);
            kern_return_t kr = vm_object_resident_insert(entry->vme_object, off, pg);
            if (kr != KERN_SUCCESS) {
                vm_page_free(pg);
                return kr;
            }
        }
    }

    paging_map_page(map->pml4, va, pg->pg_phys_addr, vm_prot_to_pte(entry->vme_protection));
    return KERN_SUCCESS;
}
