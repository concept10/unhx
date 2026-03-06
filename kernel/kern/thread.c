/*
 * kernel/kern/thread.c — Mach thread abstraction for UNHOX
 *
 * See thread.h for design rationale.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 */

#include "thread.h"
#include "task.h"
#include "kalloc.h"
#include "klib.h"

/* Static pool of threads for Phase 1 */
static struct thread thread_pool[MAX_THREADS];
static uint32_t      next_thread_id = 0;

static struct thread *thread_pool_alloc(void)
{
    for (int i = 0; i < MAX_THREADS; i++) {
        if (!thread_pool[i].th_active) {
            kmemset(&thread_pool[i], 0, sizeof(struct thread));
            thread_pool[i].th_active = 1;
            return &thread_pool[i];
        }
    }
    return (void *)0;
}

/*
 * thread_entry_trampoline — wrapper that calls the thread's entry point.
 *
 * When a new thread is first scheduled, context_switch_asm restores its
 * saved state.  The real entry_point is stashed in a callee-saved register
 * (R12 on x86-64, x19 on AArch64) so the trampoline can retrieve it after
 * the context switch.
 *
 * This trampoline exists so that if the entry function returns, we can
 * cleanly halt the thread rather than crashing.
 */
static void thread_entry_trampoline(void)
{
    void (*entry)(void);
#if defined(__aarch64__)
    __asm__ volatile ("mov %0, x19" : "=r"(entry));
#else
    __asm__ volatile ("movq %%r12, %0" : "=r"(entry));
#endif

    if (entry)
        entry();

    /* If the entry function returns, halt the thread */
    for (;;)
#if defined(__aarch64__)
        __asm__ volatile ("wfi");
#else
        __asm__ volatile ("hlt");
#endif
}

struct thread *thread_create(struct task *task,
                             void (*entry_point)(void),
                             uint32_t stack_size)
{
    if (!task)
        return (void *)0;

    struct thread *th = thread_pool_alloc();
    if (!th)
        return (void *)0;

    th->th_id    = next_thread_id++;
    th->th_state = THREAD_STATE_RUNNABLE;
    th->th_task  = task;

    /* Allocate stack */
    if (stack_size == 0)
        stack_size = THREAD_STACK_SIZE;

    void *stack = kalloc(stack_size);
    if (!stack) {
        th->th_active = 0;
        return (void *)0;
    }

    th->th_stack_base = (uint64_t)stack;
    th->th_stack_size = stack_size;
    th->th_stack_top  = (uint64_t)stack + stack_size;

    /*
     * Set up the initial CPU state so that when the scheduler first
     * switches to this thread, it begins executing at the trampoline.
     *
     * The real entry_point is stored in a callee-saved register
     * (R12 on x86-64, x19 on AArch64) so the trampoline can retrieve it
     * after the context switch restores that register.
     */
#if defined(__aarch64__)
    /*
     * AArch64: stack must be 16-byte aligned (AAPCS64 §6.2.2).
     * pc is the resume address — context_switch_asm restores it to x30
     * (lr) and returns via `ret`, jumping to thread_entry_trampoline.
     */
    th->th_cpu_state.sp  = th->th_stack_top & ~0xFULL;
    th->th_cpu_state.pc  = (uint64_t)(uintptr_t)thread_entry_trampoline;
    th->th_cpu_state.fp  = 0;
    th->th_cpu_state.x19 = (uint64_t)(uintptr_t)entry_point;
    th->th_cpu_state.x20 = 0;
    th->th_cpu_state.x21 = 0;
    th->th_cpu_state.x22 = 0;
    th->th_cpu_state.x23 = 0;
    th->th_cpu_state.x24 = 0;
    th->th_cpu_state.x25 = 0;
    th->th_cpu_state.x26 = 0;
    th->th_cpu_state.x27 = 0;
    th->th_cpu_state.x28 = 0;
#else
    /*
     * x86-64: the stack must be 16-byte aligned just before the CALL that
     * invokes thread_entry_trampoline.  We subtract 8 to account for the
     * return address that CALL would push.  context_switch_asm simulates
     * a CALL by jumping through the saved RIP.
     */
    th->th_cpu_state.rsp = th->th_stack_top & ~0xFULL;  /* align to 16 */
    th->th_cpu_state.rsp -= 8;  /* simulate call alignment */
    th->th_cpu_state.rip = (uint64_t)thread_entry_trampoline;
    th->th_cpu_state.rbp = 0;
    th->th_cpu_state.rbx = 0;
    th->th_cpu_state.r12 = (uint64_t)entry_point;
    th->th_cpu_state.r13 = 0;
    th->th_cpu_state.r14 = 0;
    th->th_cpu_state.r15 = 0;
#endif

    /* Scheduler defaults */
    th->th_priority = 10;       /* default mid-range priority */
    th->th_quantum  = 10;       /* 10 ticks per time slice */

    /* Link into the task's thread list */
    th->th_task_next = task->t_threads;
    task->t_threads  = th;
    task->t_thread_count++;

    th->th_sched_next = (void *)0;

    return th;
}

void thread_destroy(struct thread *th)
{
    if (!th || !th->th_active)
        return;

    th->th_state  = THREAD_STATE_HALTED;
    th->th_active = 0;

    /*
     * TODO: Remove from task's thread list.
     * TODO: Remove from scheduler run queue if enqueued.
     * TODO: Free the stack (requires real kfree in Phase 2).
     */
}

void thread_switch(struct thread *from, struct thread *to)
{
    if (!from || !to)
        return;

    if (from == to)
        return;

    from->th_state = THREAD_STATE_RUNNABLE;
    to->th_state   = THREAD_STATE_RUNNING;

    context_switch_asm(&from->th_cpu_state, &to->th_cpu_state);
}
