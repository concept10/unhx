/*
 * kernel/kern/thread.h — Mach thread abstraction for UNHOX
 *
 * A thread is a unit of EXECUTION.  It belongs to exactly one task.
 * The scheduler selects threads to run on the CPU.
 *
 * In Mach, threads are the entities that execute code; tasks merely own
 * resources (address space, port namespace).  A task may have many threads,
 * all sharing the same resources.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 *            "Threads are the basic unit of CPU utilization."
 */

#ifndef THREAD_H
#define THREAD_H

#include "mach/mach_types.h"
#include "platform/idt.h"
#include <stdint.h>

/* Forward declarations */
struct task;

/* Maximum threads the kernel can track in Phase 1 */
#define MAX_THREADS     32

/* Default kernel stack size: 8 KB per thread */
#define THREAD_STACK_SIZE   (8 * 1024)

/*
 * Thread states
 */
typedef enum {
    THREAD_STATE_RUNNING  = 0,   /* currently executing on a CPU          */
    THREAD_STATE_RUNNABLE = 1,   /* ready to run, in the scheduler queue  */
    THREAD_STATE_WAITING  = 2,   /* blocked on IPC or synchronization     */
    THREAD_STATE_HALTED   = 3,   /* terminated                            */
} thread_state_t;

/*
 * struct cpu_state — saved x86-64 register state for context switching.
 *
 * We save the six callee-saved registers mandated by the System V AMD64 ABI
 * plus the stack pointer and instruction pointer.  The caller-saved registers
 * are already on the stack by C calling convention.
 *
 * Register roles (System V AMD64 ABI):
 *   RBX — callee-saved general purpose
 *   RBP — callee-saved frame pointer
 *   R12 — callee-saved general purpose
 *   R13 — callee-saved general purpose
 *   R14 — callee-saved general purpose
 *   R15 — callee-saved general purpose
 *   RSP — stack pointer (saved/restored during switch)
 *   RIP — instruction pointer (saved as return address on stack)
 */
struct cpu_state {
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
    uint64_t rip;
};

/*
 * struct thread — the kernel-internal thread object.
 */
struct thread {
    uint32_t            th_id;              /* unique thread identifier       */
    thread_state_t      th_state;           /* RUNNING / RUNNABLE / etc.     */
    int                 th_active;          /* 1 if allocated/in-use         */

    /* Owning task */
    struct task        *th_task;

    /* Saved CPU state for context switching */
    struct cpu_state    th_cpu_state;

    /* Stack */
    uint64_t            th_stack_base;      /* bottom of allocated stack     */
    uint64_t            th_stack_top;       /* top of stack (initial RSP)    */
    uint32_t            th_stack_size;

    /* Scheduler fields */
    uint32_t            th_priority;        /* scheduling priority (0=lowest) */
    uint32_t            th_quantum;         /* ticks remaining in time slice  */

    /* Linked list for task's thread list */
    struct thread      *th_task_next;

    /* Linked list for scheduler run queue */
    struct thread      *th_sched_next;

    /* Linked list for IPC wait queues (blocking receive) */
    struct thread      *th_wait_next;
};

/* -------------------------------------------------------------------------
 * Thread operations
 * ------------------------------------------------------------------------- */

/*
 * thread_create — create a new thread in a task.
 *
 * task:        the owning task
 * entry_point: function to begin executing (void (*)(void))
 * stack_size:  stack size in bytes (0 = use default THREAD_STACK_SIZE)
 *
 * The thread starts in RUNNABLE state; it will begin executing when the
 * scheduler selects it.
 *
 * Returns a pointer to the new thread, or NULL on failure.
 */
struct thread *thread_create(struct task *task,
                             void (*entry_point)(void),
                             uint32_t stack_size);

/*
 * thread_destroy — destroy a thread and release its resources.
 */
void thread_destroy(struct thread *th);

/*
 * thread_switch — context switch from one thread to another.
 * Saves the current thread's registers and restores the next thread's.
 * Implemented in platform/context_switch.S.
 */
void thread_switch(struct thread *from, struct thread *to);

/*
 * thread_create_user — create a ring-3 user thread.
 *
 * task:        the owning task (must have a per-task PML4 and user stack mapped)
 * user_entry:  ring-3 entry point virtual address (from ELF e_entry)
 * user_rsp:    ring-3 initial stack pointer
 *
 * A kernel stack is allocated for syscall/interrupt handling.  When first
 * scheduled, the thread enters ring 3 via iretq (user_entry_trampoline).
 */
struct thread *thread_create_user(struct task *task,
                                  uint64_t user_entry,
                                  uint64_t user_rsp);

/*
 * context_switch_asm — low-level assembly context switch.
 * Saves callee-saved registers of 'from' and restores 'to'.
 * Called by thread_switch().
 *
 * Parameters (System V AMD64 ABI):
 *   RDI = pointer to from->th_cpu_state
 *   RSI = pointer to to->th_cpu_state
 */
extern void context_switch_asm(struct cpu_state *from, struct cpu_state *to);

/* Assembly trampoline that enters ring 3 via iretq (context_switch.S). */
extern void user_entry_trampoline(void);

/* Assembly routine for returning to user space from a forked child. */
extern void fork_child_return(void);

/* Assembly trampoline for user threads with an argument (Phase 4). */
extern void user_entry_trampoline_arg(void);

/*
 * thread_create_user_with_arg — create a ring-3 user thread with an argument.
 *
 * Like thread_create_user, but also passes `arg` in RDI to the entry function
 * via R14 on the initial stack frame.  user_entry_trampoline_arg loads R14
 * into RDI before iretq.
 */
struct thread *thread_create_user_with_arg(struct task *task,
                                            uint64_t user_entry,
                                            uint64_t user_rsp,
                                            uint64_t arg);

/*
 * thread_create_fork_child — create a user thread for a fork() child process.
 *
 * Specifically for handling fork(): creates a user thread that will return
 * to the same syscall instruction point as the parent, but with RAX=0
 * (child process return value).
 *
 * task:         the child task (created by task_copy from parent)
 * parent_frame: the parent's interrupt frame at the fork() syscall
 *
 * The child thread's stack is set up so that when scheduled, it will:
 *   1. context_switch_asm will `ret` to fork_child_return
 *   2. fork_child_return will `iretq` with the modified interrupt frame
 *   3. Execution resumes in user space with RAX=0
 *
 * Returns pointer to the new thread, or NULL on failure.
 */
struct thread *thread_create_fork_child(struct task *task,
                                        struct interrupt_frame *parent_frame);

#endif /* THREAD_H */
