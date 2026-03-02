/*
 * kernel/platform/paging.h — x86-64 4-level paging for UNHOX
 *
 * Implements the Intel/AMD 4-level page table hierarchy:
 *
 *   PML4 (Page Map Level 4)
 *     └─ PDPT (Page Directory Pointer Table)
 *         └─ PD (Page Directory)
 *             └─ PT (Page Table)
 *                 └─ 4 KB physical page frame
 *
 * Virtual address breakdown (48-bit canonical, 4-level):
 *   [63:48] — sign extension of bit 47 (canonical check)
 *   [47:39] — PML4 index   (9 bits → 512 entries)
 *   [38:30] — PDPT index   (9 bits → 512 entries)
 *   [29:21] — PD index     (9 bits → 512 entries)
 *   [20:12] — PT index     (9 bits → 512 entries)
 *   [11:0]  — page offset  (12 bits → 4096 bytes)
 *
 * Reference: Intel SDM Vol. 3A §4.5 — 4-Level Paging;
 *            AMD64 APM Vol. 2 §5.3 — Long-Mode Page Translation.
 */

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/* Page table entry flags */
#define PTE_PRESENT     (1ULL << 0)   /* Page is present in memory          */
#define PTE_WRITE       (1ULL << 1)   /* Page is writable                   */
#define PTE_USER        (1ULL << 2)   /* Page is accessible from ring 3     */
#define PTE_PWT         (1ULL << 3)   /* Page-level write-through           */
#define PTE_PCD         (1ULL << 4)   /* Page-level cache disable           */
#define PTE_ACCESSED    (1ULL << 5)   /* Set by CPU on access               */
#define PTE_DIRTY       (1ULL << 6)   /* Set by CPU on write                */
#define PTE_HUGE        (1ULL << 7)   /* 2 MB page (in PD entry)            */
#define PTE_GLOBAL      (1ULL << 8)   /* Global page (not flushed on CR3)   */
#define PTE_NX          (1ULL << 63)  /* No-execute (requires EFER.NXE)     */

/* Address mask: bits [51:12] hold the physical page frame number */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

/* Number of entries per page table level (512 = 4096 / 8) */
#define PT_ENTRIES      512

/* Virtual base of the higher-half kernel (must match linker.ld) */
#define KERNEL_VIRT_BASE    0xFFFFFFFF80000000ULL

/*
 * paging_init — set up the kernel's page tables.
 *
 * - Identity-maps the first 4 MB (for early boot code that runs at
 *   physical addresses)
 * - Maps the kernel to the higher half at KERNEL_VIRT_BASE
 * - Loads CR3 with the new PML4
 *
 * mmap_addr: physical address of the Multiboot2 memory map
 * mmap_len:  length of the memory map in bytes
 */
void paging_init(uint64_t mmap_addr, uint32_t mmap_len);

/*
 * paging_map — map a single 4 KB page in the kernel's page tables.
 *
 * virt:  virtual address (must be page-aligned)
 * phys:  physical address (must be page-aligned)
 * flags: PTE_* flags (PTE_PRESENT is always set automatically)
 *
 * Allocates intermediate page table levels as needed using vm_page_alloc().
 */
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * paging_kernel_pml4_phys — return the physical address of the kernel PML4.
 * Used as the CR3 value for kernel-only tasks.
 */
uint64_t paging_kernel_pml4_phys(void);

/*
 * paging_create_task_pml4 — allocate a new PML4 for a user task.
 *
 * The new PML4 shares the kernel's identity map (PML4[0]) and higher-half
 * mapping (PML4[511]) so that kernel code works identically in all tasks.
 * User mappings go in PML4 entries 1–510.
 *
 * Returns the physical address of the new PML4 page, or 0 on failure.
 */
uint64_t paging_create_task_pml4(void);

/*
 * paging_map_page — map a single 4 KB page in an arbitrary PML4.
 *
 * pml4:  pointer to the PML4 table (physical == virtual in identity map)
 * virt:  virtual address to map
 * phys:  physical address of the page frame
 * flags: PTE_* flags
 *
 * Allocates intermediate tables (PDPT, PD, PT) as needed.
 */
void paging_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * paging_unmap_page — unmap a single 4 KB page from an arbitrary PML4.
 *
 * Clears the PTE.  Does NOT free intermediate table pages or the physical
 * page frame (the caller is responsible for that).
 */
void paging_unmap_page(uint64_t *pml4, uint64_t virt);

/*
 * paging_destroy_task_pml4 — free a per-task PML4 and all intermediate
 * user-space page table pages (PDPT, PD, PT pages allocated by
 * paging_map_page).  Does NOT free physical page frames that were mapped.
 *
 * Kernel entries (PML4[0] identity map, PML4[511] higher-half) are skipped.
 *
 * pml4_phys: physical address of the PML4 (as returned by
 *            paging_create_task_pml4).
 */
void paging_destroy_task_pml4(uint64_t pml4_phys);

#endif /* PAGING_H */
