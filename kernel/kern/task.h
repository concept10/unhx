/*
 * kernel/kern/task.h — Mach task abstraction for NEOMACH
 *
 * A Mach task is a unit of RESOURCE OWNERSHIP.  It has:
 *   - A virtual address space (vm_map)
 *   - A port namespace (ipc_space)
 *   - Zero or more threads
 *
 * A task is NOT a thread and does not execute directly.
 *
 * This is fundamentally different from UNIX processes, where the process
 * and its single thread of execution are conflated.  In Mach:
 *   - Threads execute.
 *   - Tasks own resources.
 *   - A task with no threads is valid (it just does nothing).
 *   - A task with multiple threads shares its address space and port
 *     namespace among all of them.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 *            "A task is a unit of resource allocation, comprising a paged
 *             virtual address space with a port name space."
 */

#ifndef TASK_H
#define TASK_H

#include "mach/mach_types.h"
#include <stdint.h>

/* Forward declarations */
struct vm_map;
struct ipc_space;
struct thread;

/* Maximum tasks the kernel can track in Phase 1 */
#define MAX_TASKS   16

/*
 * Task states
 */
typedef enum {
    TASK_STATE_RUNNING   = 0,   /* task is active, threads may run        */
    TASK_STATE_SUSPENDED = 1,   /* all threads suspended by task_suspend  */
    TASK_STATE_HALTED    = 2,   /* task has been destroyed                */
} task_state_t;

/*
 * struct task — the kernel-internal task object.
 *
 * CMU Mach 3.0 paper §4.1: "A task is the basic unit of resource allocation.
 * It consists of a paged virtual address space with port rights."
 */
struct task {
    uint32_t            task_id;            /* unique task identifier         */
    task_state_t        state;              /* RUNNING / SUSPENDED / HALTED   */
    int                 is_kernel_task;     /* 1 if this is the kernel task   */
    int                 active;             /* 1 if allocated/in-use          */

    /*
     * t_map — this task's virtual address space.
     * Every task has exactly one vm_map.
     */
    struct vm_map      *t_map;

    /*
     * t_ipc_space — this task's port namespace.
     * Every task has exactly one ipc_space that maps port names
     * (small integers) to kernel port objects.
     */
    struct ipc_space   *t_ipc_space;

    /*
     * t_threads — head of linked list of threads belonging to this task.
     * Threads are linked via thread->th_task_next.
     */
    struct thread      *t_threads;
    uint32_t            t_thread_count;

    /*
     * t_ref_count — reference count.
     * Prevents premature destruction while other kernel code holds a pointer.
     */
    uint32_t            t_ref_count;
};

/* -------------------------------------------------------------------------
 * Task operations
 * ------------------------------------------------------------------------- */

/*
 * task_create — create a new task.
 *
 * parent: the parent task (may be NULL for the kernel task).
 *         In real Mach, the child inherits the parent's address space
 *         structure; for Phase 1, each task gets an empty vm_map.
 *
 * Returns a pointer to the new task, or NULL on failure.
 */
struct task *task_create(struct task *parent);

/*
 * task_destroy — destroy a task and release all its resources.
 * All threads are halted, the ipc_space is torn down, and the vm_map
 * is released.
 */
void task_destroy(struct task *t);

/*
 * task_suspend — suspend all threads in the task.
 */
void task_suspend(struct task *t);

/*
 * task_resume — resume all suspended threads in the task.
 */
void task_resume(struct task *t);

/*
 * kernel_task_ptr — returns a pointer to the kernel task (task 0).
 * Only valid after kern_init() has been called.
 */
struct task *kernel_task_ptr(void);

#endif /* TASK_H */
