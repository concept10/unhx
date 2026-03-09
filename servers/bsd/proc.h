/*
 * servers/bsd/proc.h — BSD process structure for NEOMACH
 *
 * struct proc is the BSD personality server's view of a process.  It wraps
 * a Mach task (the kernel's resource-ownership unit) with the POSIX process
 * model: a PID, a parent PID, a process-group ID, credentials, an open-file
 * table, and signal state.
 *
 * MACH TASK vs BSD PROCESS:
 *
 *   Mach tasks and BSD processes have a 1:1 correspondence in NEOMACH's
 *   Phase 2, but they are managed by different subsystems:
 *
 *   - The kernel manages the Mach task: address space, IPC port namespace,
 *     threads.  It has no concept of PIDs, signals, or file descriptors.
 *
 *   - The BSD server manages the process: PID assignment, process hierarchy,
 *     fork/exec/exit/wait semantics, signals, and file descriptors.
 *
 *   A BSD process is accessed through its Mach task port; the BSD server
 *   maps task ports to struct proc entries using p_task_port.
 *
 * PROCESS TABLE:
 *
 *   Phase 2 uses a static flat array (proc_table[BSD_PID_MAX]).  The array
 *   index is the PID.  PID 0 is reserved (POSIX); PID 1 is the init process.
 *
 * Reference: McKusick et al., "The Design and Implementation of the 4.4 BSD
 *            Operating System" §4 — Processes;
 *            GNU HURD hurd/proc.h for multi-server process design.
 */

#ifndef BSD_PROC_H
#define BSD_PROC_H

#include "fd_table.h"
#include "signal.h"
#include "kern/task.h"
#include <stdint.h>

/* =========================================================================
 * pid_t — process identifier
 *
 * POSIX defines pid_t as a signed integer type.  We use int32_t since we
 * do not include <sys/types.h> in this freestanding environment.
 * ========================================================================= */

typedef int32_t pid_t;

/* =========================================================================
 * Limits
 * ========================================================================= */

/*
 * BSD_PID_MAX — maximum number of simultaneously live processes.
 *
 * Phase 2: 64 processes (matches kernel MAX_TASKS = 16 + headroom).
 */
#define BSD_PID_MAX     64

/* Reserved PIDs */
#define BSD_PID_KERNEL  0   /* PID 0 — idle/swapper (not a real process)    */
#define BSD_PID_INIT    1   /* PID 1 — init (the BSD server itself)         */

/* =========================================================================
 * Process states
 * ========================================================================= */

typedef enum {
    /*
     * PROC_STATE_ACTIVE — normal running / sleeping process.
     * The process is alive; its Mach task is running or blocked.
     */
    PROC_STATE_ACTIVE  = 0,

    /*
     * PROC_STATE_ZOMBIE — the process has called exit() but has not yet
     * been reaped by its parent (wait()/waitpid() has not been called).
     * The Mach task has been destroyed; the struct proc is kept alive to
     * preserve the exit status for the parent.
     */
    PROC_STATE_ZOMBIE  = 1,

    /*
     * PROC_STATE_HALTED — the struct proc slot is free and can be reused.
     * Set after the parent reaps the zombie.
     */
    PROC_STATE_HALTED  = 2,
} proc_state_t;

/* =========================================================================
 * struct proc — the BSD process object
 * ========================================================================= */

struct proc {
    /* --------------------------------------------------------------------- *
     * Identity
     * --------------------------------------------------------------------- */

    pid_t               p_pid;          /* this process's PID               */
    pid_t               p_ppid;         /* parent's PID                     */
    int                 p_active;       /* 1 if slot is allocated           */
    proc_state_t        p_state;        /* ACTIVE / ZOMBIE / HALTED         */

    /* --------------------------------------------------------------------- *
     * Mach task linkage
     * --------------------------------------------------------------------- */

    /*
     * p_task — the Mach task object for this process.
     * NULL after the task has been destroyed (ZOMBIE state and beyond).
     */
    struct task        *p_task;

    /*
     * p_task_port — the send right name in the BSD server's own IPC space
     * for the task's control port.  Used to send Mach control messages to
     * the task (thread creation, suspension, etc.).
     *
     * Phase 2: Not yet wired up; the BSD server and kernel task management
     * use direct C calls rather than IPC.
     */
    mach_port_name_t    p_task_port;

    /* --------------------------------------------------------------------- *
     * Process hierarchy
     * --------------------------------------------------------------------- */

    /*
     * p_exit_status — encoded exit status.
     *
     * Set by bsd_exit(); read by the parent's bsd_wait().
     * Encoded in BSD_W_EXITCODE(return_value, signal_number) format.
     */
    int32_t             p_exit_status;

    /* --------------------------------------------------------------------- *
     * File descriptors
     * --------------------------------------------------------------------- */

    struct fd_table     p_fd_table;

    /* --------------------------------------------------------------------- *
     * Signal state
     * --------------------------------------------------------------------- */

    struct proc_signals p_signals;

    /* --------------------------------------------------------------------- *
     * Credentials (Phase 2 stubs; full POSIX credentials in Phase 3)
     * --------------------------------------------------------------------- */

    uint32_t            p_uid;      /* effective user ID                    */
    uint32_t            p_gid;      /* effective group ID                   */
    uint32_t            p_euid;     /* effective UID (same as p_uid Ph.2)   */
    uint32_t            p_egid;     /* effective GID (same as p_gid Ph.2)   */
};

/* =========================================================================
 * Process table operations
 * ========================================================================= */

/*
 * proc_init — initialise the BSD process table.
 * Creates PID 1 (the init/BSD-server process itself).
 * Must be called during bsd_server_init().
 */
void proc_init(void);

/*
 * proc_alloc — allocate a new struct proc slot with a fresh PID.
 *
 * Marks the slot active; caller fills in p_ppid, p_task, etc.
 * Returns a pointer to the new proc, or NULL if the table is full.
 */
struct proc *proc_alloc(void);

/*
 * proc_free — free a struct proc slot (mark it HALTED and reset all fields).
 * Called after the parent reaps the zombie (bsd_wait()).
 */
void proc_free(struct proc *p);

/*
 * proc_find — look up a process by PID.
 * Returns a pointer to the struct proc, or NULL if not found / not active.
 */
struct proc *proc_find(pid_t pid);

/*
 * proc_find_zombie_child — find the first ZOMBIE child of parent_pid.
 * Returns NULL if no zombie child exists.
 */
struct proc *proc_find_zombie_child(pid_t parent_pid);

/*
 * proc_find_zombie_child_by_pid — find a specific zombie child.
 * Returns NULL if the child does not exist, is not a child, or is not zombie.
 */
struct proc *proc_find_zombie_child_by_pid(pid_t parent_pid, pid_t child_pid);

#endif /* BSD_PROC_H */
