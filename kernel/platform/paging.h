/*
 * kernel/platform/paging.h — x86-64 4-level paging for UNHU
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
 * paging_map — map a single 4 KB page.
 *
 * virt:  virtual address (must be page-aligned)
 * phys:  physical address (must be page-aligned)
 * flags: PTE_* flags (PTE_PRESENT is always set automatically)
 *
 * Allocates intermediate page table levels as needed using vm_page_alloc().
 */
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags);

#endif /* PAGING_H */
