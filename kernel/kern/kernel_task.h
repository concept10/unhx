/*
 * kernel/kern/kernel_task.h — Kernel bootstrap and IPC smoke test for UNHU
 *
 * The kernel task (task 0) is the first task created at boot.  It owns the
 * kernel's own port namespace and serves as the parent for all other tasks.
 *
 * create_test_tasks() is the Phase 1 milestone test: it proves that Mach IPC
 * works by creating two tasks and passing a message between them.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 */

#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

/*
 * kernel_task_init — create the kernel task (task 0) with its ipc_space.
 * Must be called early in kernel startup, after ipc_init() and vm_init().
 */
void kernel_task_init(void);

/*
 * create_test_tasks — Phase 1 IPC smoke test.
 *
 * Creates task_a and task_b, allocates a port in task_a's space with a
 * receive right, grants a send right to task_b, has task_b send a message,
 * and verifies task_a can receive it.
 *
 * Prints to serial console:
 *   [UNHU IPC] message received: hello
 *   [UNHU] Phase 1 complete. Mach IPC operational.
 */
void create_test_tasks(void);

#endif /* KERNEL_TASK_H */
