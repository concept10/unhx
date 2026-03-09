/*
 * kernel/kern/task.c — Mach task abstraction for NEOMACH
 *
 * See task.h for design rationale and the distinction between tasks and threads.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 */

#include "task.h"
#include "kalloc.h"
#include "klib.h"
#include "ipc/ipc.h"
#include "vm/vm_map.h"

/* Static pool of tasks for Phase 1 */
static struct task task_pool[MAX_TASKS];
static uint32_t   next_task_id = 0;

/* The kernel task (task 0) */
static struct task *the_kernel_task = (void *)0;

static struct task *task_pool_alloc(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!task_pool[i].active) {
            kmemset(&task_pool[i], 0, sizeof(struct task));
            task_pool[i].active = 1;
            return &task_pool[i];
        }
    }
    return (void *)0;
}

struct task *task_create(struct task *parent)
{
    (void)parent;

    struct task *t = task_pool_alloc();
    if (!t)
        return (void *)0;

    t->task_id        = next_task_id++;
    t->state          = TASK_STATE_RUNNING;
    t->is_kernel_task = 0;
    t->t_threads      = (void *)0;
    t->t_thread_count = 0;
    t->t_ref_count    = 1;

    /* Create the task's port namespace */
    t->t_ipc_space = ipc_space_create(t);
    if (!t->t_ipc_space) {
        t->active = 0;
        return (void *)0;
    }

    /*
     * Create the task's virtual address space.
     * Phase 1: all tasks share the kernel's address space (we only
     * run kernel-mode tasks).  We still create a vm_map for structural
     * correctness — it will matter when we add userspace tasks.
     */
    t->t_map = vm_map_create(0, 0xFFFFFFFF80000000ULL);
    if (!t->t_map) {
        ipc_space_destroy(t->t_ipc_space);
        t->active = 0;
        return (void *)0;
    }

    return t;
}

void task_destroy(struct task *t)
{
    if (!t || !t->active)
        return;

    t->state = TASK_STATE_HALTED;

    /*
     * TODO: Iterate t_threads and destroy each thread.
     * For Phase 1, kernel tasks don't have real threads yet.
     */

    if (t->t_ipc_space) {
        ipc_space_destroy(t->t_ipc_space);
        t->t_ipc_space = (void *)0;
    }

    /*
     * TODO: vm_map_destroy(t->t_map) when implemented.
     */
    t->t_map = (void *)0;
    t->active = 0;
}

void task_suspend(struct task *t)
{
    if (!t || !t->active)
        return;
    t->state = TASK_STATE_SUSPENDED;
    /*
     * TODO: Walk t_threads and suspend each thread.
     * Phase 1 has no preemptive scheduler, so this is a state change only.
     */
}

void task_resume(struct task *t)
{
    if (!t || !t->active)
        return;
    if (t->state == TASK_STATE_SUSPENDED)
        t->state = TASK_STATE_RUNNING;
    /*
     * TODO: Walk t_threads and resume each thread.
     */
}

struct task *kernel_task_ptr(void)
{
    return the_kernel_task;
}

/*
 * kernel_task_create — internal: create the special kernel task (task 0).
 * Called by kern_init().
 */
void kernel_task_create(void)
{
    the_kernel_task = task_create((void *)0);
    if (the_kernel_task)
        the_kernel_task->is_kernel_task = 1;
}
