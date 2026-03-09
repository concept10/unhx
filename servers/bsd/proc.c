/*
 * servers/bsd/proc.c — BSD process table for NEOMACH
 *
 * Implements the process table (proc_table[BSD_PID_MAX]) and the CRUD
 * operations on struct proc: alloc, free, find.
 * See proc.h for the design rationale.
 */

#include "proc.h"
#include "kern/klib.h"
#include "kern/kalloc.h"

/* =========================================================================
 * Process table
 * ========================================================================= */

/*
 * Static process table.  Index 0 is unused (PID 0 is the kernel/idle slot).
 * PID == table index for simplicity in Phase 2.
 */
static struct proc proc_table[BSD_PID_MAX];

/* Next PID to try when allocating.  Searches linearly; wraps around. */
static pid_t next_pid = BSD_PID_INIT;

/* =========================================================================
 * proc_init
 * ========================================================================= */

void proc_init(void)
{
    kmemset(proc_table, 0, sizeof(proc_table));

    /*
     * PID 1 — the init/BSD server process itself.
     *
     * In a full system this would be a real userspace task launched by the
     * kernel.  For Phase 2 it serves as the root of the process tree so
     * that orphaned children can be reparented.
     */
    struct proc *init = &proc_table[BSD_PID_INIT];
    init->p_pid     = BSD_PID_INIT;
    init->p_ppid    = BSD_PID_KERNEL;
    init->p_active  = 1;
    init->p_state   = PROC_STATE_ACTIVE;
    init->p_task    = (void *)0;  /* kernel_task_ptr() could go here */
    init->p_uid     = 0;
    init->p_gid     = 0;
    init->p_euid    = 0;
    init->p_egid    = 0;

    fd_table_init(&init->p_fd_table);
    proc_signals_init(&init->p_signals);
}

/* =========================================================================
 * proc_alloc
 * ========================================================================= */

struct proc *proc_alloc(void)
{
    pid_t start = next_pid;
    pid_t pid;

    /*
     * Search for a free slot starting from next_pid.
     * Skip PID 0 (kernel) and PID 1 (init, always resident).
     */
    for (pid = start; pid < BSD_PID_MAX; pid++) {
        if (pid < 2) continue;
        if (!proc_table[pid].p_active)
            goto found;
    }
    for (pid = 2; pid < start; pid++) {
        if (!proc_table[pid].p_active)
            goto found;
    }
    return (void *)0;   /* process table full */

found:
    kmemset(&proc_table[pid], 0, sizeof(struct proc));
    proc_table[pid].p_pid    = pid;
    proc_table[pid].p_active = 1;
    proc_table[pid].p_state  = PROC_STATE_ACTIVE;

    fd_table_init(&proc_table[pid].p_fd_table);
    proc_signals_init(&proc_table[pid].p_signals);

    next_pid = (pid + 1 < BSD_PID_MAX) ? (pid + 1) : 2;
    return &proc_table[pid];
}

/* =========================================================================
 * proc_free
 * ========================================================================= */

void proc_free(struct proc *p)
{
    if (!p || !p->p_active)
        return;

    /* Close any remaining open file descriptors */
    fd_table_close_all(&p->p_fd_table);

    kmemset(p, 0, sizeof(struct proc));
    /* p_active is now 0; the slot is free */
}

/* =========================================================================
 * proc_find
 * ========================================================================= */

struct proc *proc_find(pid_t pid)
{
    if (pid < 0 || pid >= BSD_PID_MAX)
        return (void *)0;
    if (!proc_table[pid].p_active)
        return (void *)0;
    if (proc_table[pid].p_state == PROC_STATE_HALTED)
        return (void *)0;
    return &proc_table[pid];
}

/* =========================================================================
 * proc_find_zombie_child
 * ========================================================================= */

struct proc *proc_find_zombie_child(pid_t parent_pid)
{
    pid_t pid;

    for (pid = 2; pid < BSD_PID_MAX; pid++) {
        struct proc *p = &proc_table[pid];
        if (p->p_active &&
            p->p_ppid == parent_pid &&
            p->p_state == PROC_STATE_ZOMBIE)
            return p;
    }
    return (void *)0;
}

/* =========================================================================
 * proc_find_zombie_child_by_pid
 * ========================================================================= */

struct proc *proc_find_zombie_child_by_pid(pid_t parent_pid, pid_t child_pid)
{
    struct proc *p = proc_find(child_pid);

    if (!p)
        return (void *)0;
    if (p->p_ppid != parent_pid)
        return (void *)0;
    if (p->p_state != PROC_STATE_ZOMBIE)
        return (void *)0;

    return p;
}
