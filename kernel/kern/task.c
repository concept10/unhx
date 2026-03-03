/*
 * kernel/kern/task.c — Mach task abstraction for UNHOX
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
#include "platform/paging.h"

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

    /*
     * Set the task's page tables.
     * Kernel tasks use the kernel PML4 directly.  User tasks will
     * call paging_create_task_pml4() to get a per-task PML4.
     */
    t->t_cr3 = paging_kernel_pml4_phys();

    /* Link the vm_map to the task's page tables */
    t->t_map->pml4 = (uint64_t *)t->t_cr3;

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
     * Free per-task page tables if this is not the kernel task.
     * The kernel PML4 is statically allocated and must not be freed.
     */
    if (!t->is_kernel_task && t->t_cr3 != paging_kernel_pml4_phys()) {
        paging_destroy_task_pml4(t->t_cr3);
    }
    t->t_cr3 = 0;

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
/*
 * task_copy — copy a task for fork().
 *
 * Creates a new task that is a copy of the parent task:
 * - Allocates a new task struct
 * - Creates a per-task PML4 (separate address space)
 * - Copies all vm_map entries from parent to child
 * - Allocates physical pages for each entry
 * - Creates a new ipc_space for the child (isolated from parent)
 *
 * The child task is created in RUNNING state but no thread is created;
 * the BSD server's fork handler will create the initial thread.
 *
 * Returns a pointer to the new child task, or NULL on failure.
 */
struct task *task_copy(struct task *parent)
{
    if (!parent || !parent->active)
        return (void *)0;

    /* Allocate a new task slot */
    struct task *child = task_pool_alloc();
    if (!child)
        return (void *)0;

    child->task_id        = next_task_id++;
    child->state          = TASK_STATE_RUNNING;
    child->is_kernel_task = 0;
    child->t_threads      = (void *)0;
    child->t_thread_count = 0;
    child->t_ref_count    = 1;

    /* Create a new IPC space for the child (isolated from parent) */
    child->t_ipc_space = ipc_space_create(child);
    if (!child->t_ipc_space) {
        child->active = 0;
        return (void *)0;
    }

    /* Create a new per-task PML4 for the child's address space */
    child->t_cr3 = paging_create_task_pml4();
    if (child->t_cr3 == 0) {
        ipc_space_destroy(child->t_ipc_space);
        child->active = 0;
        return (void *)0;
    }

    /* Create a new vm_map for the child */
    child->t_map = vm_map_create(0, 0xFFFFFFFF80000000ULL);
    if (!child->t_map) {
        ipc_space_destroy(child->t_ipc_space);
        paging_destroy_task_pml4(child->t_cr3);
        child->active = 0;
        return (void *)0;
    }

    /* Link the vm_map to the child's page tables */
    child->t_map->pml4 = (uint64_t *)child->t_cr3;

    /* Copy each vm_map entry from parent to child */
    for (uint32_t i = 0; i < parent->t_map->entry_count; i++) {
        struct vm_map_entry *parent_entry = &parent->t_map->entries[i];

        if (!parent_entry->vme_in_use)
            continue;

        /* Enter the same range in the child's vm_map */
        kern_return_t kr = vm_map_enter(
            child->t_map,
            parent_entry->vme_start,
            parent_entry->vme_end - parent_entry->vme_start,
            parent_entry->vme_protection
        );

        if (kr != KERN_SUCCESS) {
            /* Cleanup on failure */
            task_destroy(child);
            return (void *)0;
        }
    }

    return child;
}

void kernel_task_create(void)
{
    the_kernel_task = task_create((void *)0);
    if (the_kernel_task)
        the_kernel_task->is_kernel_task = 1;
}
