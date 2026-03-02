/*
 * kernel/kern/elf_load.c — ELF64 loader for UNHOX
 *
 * Loads a statically-linked ELF64 executable into a task's address space.
 * Only PT_LOAD segments are processed.  The loader:
 *   1. Validates the ELF header (magic, class, machine, type).
 *   2. For each PT_LOAD segment, allocates pages in the task's vm_map
 *      and copies the segment data from the in-memory ELF image.
 *   3. Returns the entry point address.
 *
 * The ELF image is expected to be in kernel memory (e.g. loaded by the
 * bootloader as a Multiboot module via -initrd).
 *
 * Reference: System V ABI — ELF Specification, AMD64 supplement.
 */

#include "elf.h"
#include "task.h"
#include "klib.h"
#include "vm/vm_map.h"
#include "vm/vm_page.h"
#include "platform/paging.h"

extern void serial_putstr(const char *s);

/*
 * elf_load — load an ELF64 binary into a task's address space.
 *
 * task:        the target task (must have a vm_map with pml4)
 * data:        pointer to the ELF image in kernel memory
 * size:        size of the ELF image in bytes
 * entry_out:   [out] receives the entry point virtual address
 *
 * Returns KERN_SUCCESS on success.
 */
kern_return_t elf_load(struct task *task,
                       const void *data,
                       uint64_t size,
                       uint64_t *entry_out)
{
    if (!task || !data || !entry_out)
        return KERN_INVALID_ARGUMENT;

    if (size < sizeof(Elf64_Ehdr))
        return KERN_INVALID_ARGUMENT;

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    /* Validate ELF magic */
    uint32_t magic = *(const uint32_t *)ehdr->e_ident;
    if (magic != ELF_MAGIC) {
        serial_putstr("[elf] bad magic\r\n");
        return KERN_INVALID_ARGUMENT;
    }

    /* Validate class (64-bit), data (little-endian), machine (x86-64) */
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA]  != ELFDATA2LSB ||
        ehdr->e_machine         != EM_X86_64 ||
        ehdr->e_type            != ET_EXEC) {
        serial_putstr("[elf] unsupported ELF format\r\n");
        return KERN_INVALID_ARGUMENT;
    }

    /* Validate program header table */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        serial_putstr("[elf] no program headers\r\n");
        return KERN_INVALID_ARGUMENT;
    }

    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > size) {
        serial_putstr("[elf] program headers beyond image\r\n");
        return KERN_INVALID_ARGUMENT;
    }

    struct vm_map *map = task->t_map;
    if (!map || !map->pml4) {
        serial_putstr("[elf] task has no vm_map\r\n");
        return KERN_INVALID_ARGUMENT;
    }

    /* Process each program header */
    const uint8_t *base = (const uint8_t *)data;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = (const Elf64_Phdr *)(base + ehdr->e_phoff +
                                                       i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD)
            continue;

        if (phdr->p_memsz == 0)
            continue;

        /* Compute page-aligned virtual range */
        uint64_t seg_start = phdr->p_vaddr & ~(uint64_t)(VM_PAGE_SIZE - 1);
        uint64_t seg_end   = (phdr->p_vaddr + phdr->p_memsz + VM_PAGE_SIZE - 1)
                             & ~(uint64_t)(VM_PAGE_SIZE - 1);
        uint64_t seg_size  = seg_end - seg_start;

        /* Convert ELF p_flags to VM protection */
        vm_prot_t prot = VM_PROT_NONE;
        if (phdr->p_flags & PF_R) prot |= VM_PROT_READ;
        if (phdr->p_flags & PF_W) prot |= VM_PROT_WRITE;
        if (phdr->p_flags & PF_X) prot |= VM_PROT_EXECUTE;

        /* Allocate the region in the task's address space */
        kern_return_t kr = vm_map_enter(map, seg_start, seg_size, prot);
        if (kr != KERN_SUCCESS) {
            serial_putstr("[elf] vm_map_enter failed\r\n");
            return kr;
        }

        /*
         * Copy segment data from the ELF image into the mapped pages.
         *
         * We walk the page tables to find each physical page and copy
         * into it directly (since we're identity-mapped, phys == virt
         * for kernel access).
         */
        uint64_t file_off  = phdr->p_offset;
        uint64_t file_size = phdr->p_filesz;
        uint64_t vaddr     = phdr->p_vaddr;

        /* Validate file data bounds */
        if (file_off + file_size > size)
            file_size = (file_off < size) ? size - file_off : 0;

        for (uint64_t copied = 0; copied < phdr->p_memsz; ) {
            uint64_t page_va    = (vaddr + copied) & ~(uint64_t)(VM_PAGE_SIZE - 1);
            uint64_t page_off   = (vaddr + copied) & (VM_PAGE_SIZE - 1);
            uint64_t chunk_size = VM_PAGE_SIZE - page_off;

            if (chunk_size > phdr->p_memsz - copied)
                chunk_size = phdr->p_memsz - copied;

            /*
             * Walk the page tables to get the physical address.
             * Since we're identity-mapped, we can write to phys directly.
             */
            uint64_t *pml4 = map->pml4;
            uint64_t phys = 0;

            /* PML4 → PDPT → PD → PT → page */
            uint64_t e4 = pml4[(page_va >> 39) & 0x1FF];
            if (e4 & PTE_PRESENT) {
                uint64_t *pdpt = (uint64_t *)(e4 & PTE_ADDR_MASK);
                uint64_t e3 = pdpt[(page_va >> 30) & 0x1FF];
                if (e3 & PTE_PRESENT) {
                    uint64_t *pd = (uint64_t *)(e3 & PTE_ADDR_MASK);
                    uint64_t e2 = pd[(page_va >> 21) & 0x1FF];
                    if ((e2 & PTE_PRESENT) && !(e2 & PTE_HUGE)) {
                        uint64_t *pt = (uint64_t *)(e2 & PTE_ADDR_MASK);
                        uint64_t e1 = pt[(page_va >> 12) & 0x1FF];
                        if (e1 & PTE_PRESENT)
                            phys = (e1 & PTE_ADDR_MASK) + page_off;
                    }
                }
            }

            if (phys) {
                if (copied < file_size) {
                    uint64_t data_chunk = chunk_size;
                    if (copied + data_chunk > file_size)
                        data_chunk = file_size - copied;
                    kmemcpy((void *)phys, base + file_off + copied, data_chunk);
                    /* Zero remainder of chunk if file data is shorter */
                    if (data_chunk < chunk_size)
                        kmemset((void *)(phys + data_chunk), 0,
                                chunk_size - data_chunk);
                } else {
                    /* BSS: zero-fill */
                    kmemset((void *)phys, 0, chunk_size);
                }
            }

            copied += chunk_size;
        }
    }

    *entry_out = ehdr->e_entry;
    return KERN_SUCCESS;
}
