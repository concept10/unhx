/*
 * kernel/vm/vm_fault.h — Page fault handling for UNHOX
 */

#ifndef VM_FAULT_H
#define VM_FAULT_H

#include "mach/mach_types.h"

struct interrupt_frame;

/*
 * vm_fault_handle — attempt to resolve a page fault for the current thread.
 *
 * fault_addr: linear address from CR2
 * error_code: x86-64 page-fault error code
 *
 * Returns KERN_SUCCESS if the fault was handled and execution may resume.
 */
kern_return_t vm_fault_handle(struct interrupt_frame *frame,
                              uint64_t fault_addr,
                              uint64_t error_code);

#endif /* VM_FAULT_H */
