/*
 * kernel/vm/vm_page.c — Physical page frame allocator for UNHU
 *
 * See vm_page.h for interface documentation.
 */

#include "vm_page.h"

/* Static array of page descriptors */
static struct vm_page page_array[VM_PAGE_MAX];

/* Head of the free page list */
static struct vm_page *free_list;
static uint32_t        free_count;
static uint32_t        total_pages;

void vm_page_init(uint64_t base, uint64_t size)
{
    /* Align base up to page boundary */
    uint64_t aligned_base = (base + VM_PAGE_SIZE - 1) & ~(uint64_t)(VM_PAGE_SIZE - 1);
    uint64_t end = base + size;

    free_list   = (void *)0;
    free_count  = 0;
    total_pages = 0;

    for (uint64_t addr = aligned_base;
         addr + VM_PAGE_SIZE <= end && total_pages < VM_PAGE_MAX;
         addr += VM_PAGE_SIZE)
    {
        struct vm_page *pg = &page_array[total_pages];
        pg->pg_phys_addr = addr;
        pg->pg_flags     = VM_PAGE_FREE;

        /* Push onto free list */
        pg->pg_next = free_list;
        free_list   = pg;

        free_count++;
        total_pages++;
    }
}

struct vm_page *vm_page_alloc(void)
{
    if (!free_list)
        return (void *)0;

    struct vm_page *pg = free_list;
    free_list = pg->pg_next;
    pg->pg_next  = (void *)0;
    pg->pg_flags = VM_PAGE_ALLOCATED;
    free_count--;

    return pg;
}

void vm_page_free(struct vm_page *page)
{
    if (!page)
        return;

    page->pg_flags = VM_PAGE_FREE;
    page->pg_next  = free_list;
    free_list      = page;
    free_count++;
}

uint32_t vm_page_count_free(void)
{
    return free_count;
}
