/*
 * kernel/kern/kern.h — Kernel core: tasks, threads, and scheduler (NEOMACH)
 *
 * This subsystem implements the Mach task and thread abstractions:
 *
 *   task   — a unit of resource ownership.  A task has a virtual address
 *             space (vm_map) and a port namespace (ipc_space).  It is NOT a
 *             thread and does not execute directly.
 *
 *   thread — a unit of execution.  A thread belongs to exactly one task.
 *             The scheduler selects threads to run on the CPU.
 *
 *   sched  — a round-robin scheduler for Phase 1.  The full Mach scheduler
 *             (multi-level feedback queue with priority decay, Tevanian et al.)
 *             is deferred to Phase 2.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 */

#ifndef KERN_H
#define KERN_H

#include "mach/mach_types.h"

/*
 * kern_init — initialise the kernel core subsystems.
 * Creates the kernel task (task 0) and the idle thread.
 */
void kern_init(void);

/*
 * kernel_main — C entry point jumped to from boot.S after hardware setup.
 * Initialises all subsystems and enters the scheduler loop.
 */
void kernel_main(void);

#endif /* KERN_H */
