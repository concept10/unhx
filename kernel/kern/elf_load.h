/*
 * kernel/kern/elf_load.h — ELF64 loader interface for UNHOX
 */

#ifndef ELF_LOAD_H
#define ELF_LOAD_H

#include "mach/mach_types.h"
#include <stdint.h>

struct task;

/*
 * elf_load — load an ELF64 executable into a task's address space.
 *
 * task:      target task (must have vm_map with pml4 set)
 * data:      pointer to ELF image in kernel memory
 * size:      size of the ELF image
 * entry_out: [out] entry point virtual address
 *
 * Returns KERN_SUCCESS on success.
 */
kern_return_t elf_load(struct task *task,
                       const void *data,
                       uint64_t size,
                       uint64_t *entry_out);

#endif /* ELF_LOAD_H */
