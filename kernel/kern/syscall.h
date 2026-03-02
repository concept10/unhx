/*
 * kernel/kern/syscall.h — System call definitions for UNHOX
 *
 * User programs invoke system calls via `int $0x80`.  The syscall number
 * is in RAX; arguments are in RDI, RSI, RDX, R10, R8, R9.  The return
 * value is placed in RAX by the kernel.
 *
 * Syscall numbers:
 *   0 — SYS_MACH_MSG   (Mach IPC — send/receive)
 *   1 — SYS_WRITE      (write bytes to serial console)
 *   2 — SYS_EXIT       (terminate the current task)
 *   3 — SYS_READ       (read bytes from serial console)
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* Syscall numbers */
#define SYS_MACH_MSG    0
#define SYS_WRITE       1
#define SYS_EXIT        2
#define SYS_READ        3

struct interrupt_frame;

/*
 * syscall_dispatch — handle a system call.
 * Called from isr_dispatch() when vector == 0x80.
 * The syscall number is in frame->rax.
 */
void syscall_dispatch(struct interrupt_frame *frame);

#endif /* SYSCALL_H */
