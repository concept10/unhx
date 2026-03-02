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
 * saved state.  We set up RIP to point here, with the real entry_point
 * stored in R12 (a callee-saved register preserved across the switch).
 *
 * This trampoline exists so that if the entry function returns, we can
 * cleanly halt the thread rather than crashing.
 */
static void thread_entry_trampoline(void)
{
    /*
     * Enable interrupts.  New threads are "born" from a context switch
     * that may have occurred inside a timer ISR (where IF is cleared).
     * We must re-enable interrupts so the thread can be preempted.
     */
    __asm__ volatile ("sti");

    /*
     * The real entry point was stashed in R12 by thread_create().
     * We extract it via inline assembly and call it.
     */
    void (*entry)(void);
    __asm__ volatile ("movq %%r12, %0" : "=r"(entry));

    if (entry)
        entry();

    /* If the entry function returns, halt the thread */
    for (;;)
        __asm__ volatile ("hlt");
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
     * Build the initial stack frame so that the first context switch
     * (via context_switch_asm) correctly "returns" to the trampoline.
     *
     * context_switch_asm restores by popping 6 callee-saved registers
     * (r15, r14, r13, r12, rbp, rbx) then `ret`.  We pre-build this
     * exact stack layout:
     *
     *   [padding]                    ← 16-byte alignment padding
     *   [thread_entry_trampoline]    ← return address (popped by ret)
     *   [rbx = 0]
     *   [rbp = 0]
     *   [r12 = entry_point]          ← trampoline reads this
     *   [r13 = 0]
     *   [r14 = 0]
     *   [r15 = 0]                    ← RSP points here
     *
     * After the 6 pops + ret, RSP = &padding, which is 16-aligned - 8,
     * satisfying the System V AMD64 ABI function-entry alignment rule.
     */
    uint64_t sp = th->th_stack_top & ~0xFULL;    /* align to 16 */

    sp -= 8; *(uint64_t *)sp = 0;                /* alignment padding */
    sp -= 8; *(uint64_t *)sp = (uint64_t)thread_entry_trampoline;  /* ret addr */
    sp -= 8; *(uint64_t *)sp = 0;                /* rbx */
    sp -= 8; *(uint64_t *)sp = 0;                /* rbp */
    sp -= 8; *(uint64_t *)sp = (uint64_t)entry_point;  /* r12 */
    sp -= 8; *(uint64_t *)sp = 0;                /* r13 */
    sp -= 8; *(uint64_t *)sp = 0;                /* r14 */
    sp -= 8; *(uint64_t *)sp = 0;                /* r15 */

    th->th_cpu_state.rsp = sp;

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
