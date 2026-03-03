/*
 * kernel/kern/sched.h — Round-robin scheduler for UNHOX (Phase 1)
 *
 * WARNING: This is NOT the Mach scheduler.
 *
 * The real Mach scheduler (Tevanian et al.) uses a multi-level feedback
 * queue with thread priorities and priority decay.  Threads that use their
 * full time quantum see their priority decayed to prevent CPU starvation;
 * interactive threads that block on IPC get priority boosts to maintain
 * responsiveness.
 *
 * This Phase 1 scheduler is a trivial round-robin implementation — just
 * enough to run kernel test threads and verify context switching works.
 *
 * TODO (Phase 2): Implement the full Mach scheduler with:
 *   - 128 priority levels (0 = lowest, 127 = highest)
 *   - Multi-level feedback queue (one run queue per priority)
 *   - Priority decay: quantum-expiry → priority decremented
 *   - Priority boost: IPC wakeup → priority restored
 *   - Handoff scheduling: combined send+receive switches directly
 *     to the receiver thread without going through the run queue
 *   Reference: Tevanian & Smith, "Mach Threads and the Unix Kernel:
 *              The Battle for Control", USENIX, 1987.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4.3 — Scheduling.
 */

#ifndef SCHED_H
#define SCHED_H

#include "thread.h"

/*
 * Default time quantum in ticks.  At ~100 Hz timer this gives ~100 ms
 * per thread before preemption.
 */
#define SCHED_DEFAULT_QUANTUM   10

/* -------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

/*
 * sched_init — initialise the scheduler.
 * Sets up the run queue and configures the timer interrupt (PIT) for
 * preemptive scheduling.
 */
void sched_init(void);

/*
 * sched_enqueue — add a thread to the run queue.
 * Thread must be in RUNNABLE state.
 */
void sched_enqueue(struct thread *th);

/*
 * sched_dequeue — remove and return the next thread from the run queue.
 * Returns NULL if the queue is empty.
 */
struct thread *sched_dequeue(void);

/*
 * sched_tick — called from the timer interrupt handler.
 * Decrements the current thread's quantum.  When the quantum expires,
 * calls sched_yield() to switch to the next thread.
 */
void sched_tick(void);

/*
 * sched_yield — voluntarily yield the CPU.
 * Puts the current thread at the back of the run queue and switches
 * to the next runnable thread.
 */
void sched_yield(void);

/*
 * sched_current — return a pointer to the currently running thread.
 */
struct thread *sched_current(void);

/*
 * sched_set_current — set the current thread (used during bootstrap).
 */
void sched_set_current(struct thread *th);

/*
 * sched_sleep — put the current thread to sleep (WAITING state).
 * The thread is removed from the run queue and will not be scheduled
 * until sched_wakeup() is called on it.  Used by blocking IPC receive.
 */
void sched_sleep(void);

/*
 * sched_wakeup — wake a sleeping thread.
 * Sets the thread to RUNNABLE state and places it on the run queue.
 * Called by IPC send when a message arrives for a blocked receiver.
 */
void sched_wakeup(struct thread *th);

/*
 * sched_yield_if_waiting — yield the CPU only if still in WAITING state.
 *
 * Called by ipc_mqueue_receive after setting th_state=WAITING (under the
 * mqueue lock) and releasing the lock.  Between the unlock and this call,
 * sched_wakeup() may already have run and set our state back to RUNNABLE.
 * In that case we return immediately so the caller can retry the dequeue.
 *
 * This closes the race in sched_sleep() where:
 *   1. mqueue_unlock() re-enables IRQs (state still RUNNABLE)
 *   2. sender calls sched_wakeup() → sees RUNNABLE → does nothing
 *   3. sched_sleep() sets WAITING → thread blocks forever
 *
 * By setting state=WAITING before mqueue_unlock and using this function
 * instead of sched_sleep(), the wakeup is never missed.
 */
void sched_yield_if_waiting(void);

/*
 * sched_run — enable interrupts and enter the idle loop.
 * Called at the end of kernel_main() after all threads are created.
 * This function never returns.
 */
void sched_run(void);

#endif /* SCHED_H */
