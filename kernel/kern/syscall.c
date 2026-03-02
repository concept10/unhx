/*
 * kernel/kern/syscall.c — System call handler for UNHOX
 *
 * Dispatches system calls from ring-3 user programs.
 * User programs invoke `int $0x80` with:
 *   RAX = syscall number
 *   RDI, RSI, RDX, RCX = arguments
 *
 * The return value is placed in frame->rax.
 */

#include "syscall.h"
#include "sched.h"
#include "task.h"
#include "thread.h"
#include "platform/idt.h"
#include "kern.h"
#include "elf.h"
#include "bsd/bsd_msg.h"

extern void serial_putstr(const char *s);
extern void serial_putchar(char c);
extern char serial_getchar(void);

/* -------------------------------------------------------------------------
 * SYS_WRITE — write a string to the serial console.
 *
 * RDI = pointer to buffer (user address)
 * RSI = length in bytes
 *
 * Returns the number of bytes written.
 *
 * TODO: validate user pointer against the task's vm_map.
 * For Phase 2, we trust the user pointer (kernel tasks only initially).
 * ------------------------------------------------------------------------- */

static int64_t sys_write(struct interrupt_frame *frame)
{
    const char *buf = (const char *)frame->rdi;
    uint64_t len = frame->rsi;

    /* Basic safety: reject null or obviously bad pointers */
    if (!buf || len > 0x100000)
        return -1;

    for (uint64_t i = 0; i < len; i++)
        serial_putchar(buf[i]);

    return (int64_t)len;
}

/* -------------------------------------------------------------------------
 * SYS_READ — read a character from the serial console.
 *
 * RDI = pointer to buffer (user address)
 * RSI = max length
 *
 * Returns the number of bytes read (1 on success, 0 if no data).
 * ------------------------------------------------------------------------- */

static int64_t sys_read(struct interrupt_frame *frame)
{
    char *buf = (char *)frame->rdi;
    uint64_t len = frame->rsi;

    if (!buf || len == 0)
        return -1;

    char c = serial_getchar();
    if (c == 0)
        return 0;  /* no data available */

    buf[0] = c;
    return 1;
}

/* -------------------------------------------------------------------------
 * SYS_EXIT — terminate the current task.
 *
 * RDI = exit status (unused for now)
 * ------------------------------------------------------------------------- */

static void sys_exit(struct interrupt_frame *frame)
{
    (void)frame;
    struct thread *th = sched_current();
    if (th) {
        th->th_state = THREAD_STATE_HALTED;
        sched_yield();
    }
    /* Should not reach here */
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* -------------------------------------------------------------------------
 * SYS_FORK — fork the current task (Phase 2 stub).
 *
 * Arguments: (none)
 *
 * Returns: child task ID in parent, 0 in child (when fully implemented).
 *
 * Phase 2 Implementation:
 *   - Creates a new task with a copy of the parent's address space
 *   - Does NOT yet create a thread in the child
 *   - Returns child task ID to parent
 *   - Full context copying for the child thread deferred to Phase 3
 *
 * Real Implementation (Phase 3+):
 *   The child task will be created with a new thread that resumes at the
 *   syscall return point with RAX = 0 (child return value).  This requires:
 *   - Copying the parent thread's register state
 *   - Setting up the child thread's stack/RIP to return from int $0x80
 *   - Proper parent-child task linking for wait()
 * --------- ------- ------------------------------------------------- */

static int64_t sys_fork(struct interrupt_frame *frame)
{
    (void)frame;
    
    struct thread *parent_th = sched_current();
    if (!parent_th)
        return -1;

    struct task *parent = parent_th->th_task;
    if (!parent || !parent->active)
        return -1;

    /* Clone the parent task (copy vm_map and create new ipc_space) */
    struct task *child = task_copy(parent);
    if (!child)
        return -1;

    /* For Phase 2, we do not yet create a thread in the child.
     * Phase 3 will implement proper thread creation and context setup
     * so that the child resumes at the syscall return with RAX = 0.
     *
     * Returning the child task ID to the parent; child task will be
     * runnable once we implement thread creation below.
     */

    return (int64_t)child->task_id;
}

/* -------------------------------------------------------------------------
 * SYS_EXEC — load and execute a new ELF binary in the current task.
 *
 * RDI = pointer to path string (kernel memory for now)
 * RSI = argv
 * RDX = envp
 *
 * Returns: -1 on error.  On success, does not return (jumps to new entry).
 *
 * TODO (Phase 3):
 *   - Look up the executable in the VFS server
 *   - Handle interpreter (shebang) scripts
 *   - Set up argc/argv on the new stack
 * --------- ------- ------------------------------------------------- */

static int64_t sys_exec(struct interrupt_frame *frame)
{
    const char *path = (const char *)frame->rdi;
    if (!path || path[0] == '\0') {
        return -1;
    }

    return (int64_t)bsd_exec_current(path, frame);
}

/* -------------------------------------------------------------------------
 * SYS_WAIT — wait for a child task to exit and reap it.
 *
 * RDI = child task ID (for now; real Mach uses pid)
 * RSI = status out (pointer to int where exit status is stored)
 *
 * Returns: child task ID on success, -1 on error.
 *
 * TODO (Phase 3):
 *   - Implement proper parent-child relationship tracking
 *   - Handle multiple children
 *   - Return exit status correctly
 * --------- ------- ------------------------------------------------- */

static int64_t sys_wait(struct interrupt_frame *frame)
{
    (void)frame;
    /* Deferred to Phase 3 — requires process table management */
    return -1;
}

/* -------------------------------------------------------------------------
 * syscall_dispatch — main system call dispatcher.
 * --------- ------- ------------------------------------------------- */

void syscall_dispatch(struct interrupt_frame *frame)
{
    uint64_t syscall_nr = frame->rax;

    switch (syscall_nr) {
    case SYS_WRITE:
        frame->rax = (uint64_t)sys_write(frame);
        break;

    case SYS_READ:
        frame->rax = (uint64_t)sys_read(frame);
        break;

    case SYS_EXIT:
        sys_exit(frame);
        break;

    case SYS_FORK:
        frame->rax = (uint64_t)sys_fork(frame);
        break;

    case SYS_EXEC:
        frame->rax = (uint64_t)sys_exec(frame);
        break;

    case SYS_WAIT:
        frame->rax = (uint64_t)sys_wait(frame);
        break;

    case SYS_MACH_MSG:
        /* TODO: implement mach_msg syscall */
        frame->rax = (uint64_t)-1;
        break;

    default:
        frame->rax = (uint64_t)-1;
        break;
    }
}
