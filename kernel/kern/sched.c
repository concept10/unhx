/*
 * kernel/kern/sched.c — Round-robin scheduler for UNHU (Phase 1)
 *
 * See sched.h for design rationale and Phase 2 TODO markers.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4.3 — Scheduling.
 */

#include "sched.h"

/* The run queue: a singly-linked list of runnable threads */
static struct thread *run_queue_head = (void *)0;
static struct thread *run_queue_tail = (void *)0;

/* The currently executing thread */
static struct thread *current_thread = (void *)0;

/* -------------------------------------------------------------------------
 * I/O port helpers (for PIT timer setup)
 * ------------------------------------------------------------------------- */

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* -------------------------------------------------------------------------
 * PIT (Programmable Interval Timer) setup
 *
 * The x86 PIT (8253/8254) generates periodic interrupts on IRQ 0.
 * We program channel 0 for ~100 Hz (10 ms intervals).
 *
 * PIT base frequency: 1,193,182 Hz
 * Divisor for 100 Hz: 1193182 / 100 = 11932 (0x2E9C)
 *
 * TODO (Phase 2): Use the LAPIC timer instead of the PIT for per-CPU
 *                 timing on SMP systems.
 * ------------------------------------------------------------------------- */

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND         0x43

static void pit_init(void)
{
    uint16_t divisor = 11932;  /* ~100 Hz */

    /*
     * Command byte: channel 0, access mode lobyte/hibyte,
     * mode 2 (rate generator), binary counting
     */
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0_DATA, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (unsigned char)((divisor >> 8) & 0xFF));

    /*
     * NOTE: The PIT is now configured but IRQ 0 won't fire until we:
     *   1. Set up the IDT with an IRQ 0 handler
     *   2. Program the PIC to unmask IRQ 0
     *   3. Enable interrupts (STI)
     *
     * For Phase 1, we use cooperative scheduling (sched_yield) and do
     * not enable timer interrupts.  The PIT setup is here as preparation
     * for Phase 2 preemptive scheduling.
     *
     * TODO (Phase 2): Set up IDT entry for IRQ 0, PIC init, STI.
     */
}

/* -------------------------------------------------------------------------
 * Scheduler implementation
 * ------------------------------------------------------------------------- */

void sched_init(void)
{
    run_queue_head = (void *)0;
    run_queue_tail = (void *)0;
    current_thread = (void *)0;

    pit_init();
}

void sched_enqueue(struct thread *th)
{
    if (!th)
        return;

    th->th_sched_next = (void *)0;

    if (run_queue_tail) {
        run_queue_tail->th_sched_next = th;
    } else {
        run_queue_head = th;
    }
    run_queue_tail = th;
}

struct thread *sched_dequeue(void)
{
    if (!run_queue_head)
        return (void *)0;

    struct thread *th = run_queue_head;
    run_queue_head = th->th_sched_next;
    th->th_sched_next = (void *)0;

    if (!run_queue_head)
        run_queue_tail = (void *)0;

    return th;
}

void sched_tick(void)
{
    if (!current_thread)
        return;

    if (current_thread->th_quantum > 0)
        current_thread->th_quantum--;

    if (current_thread->th_quantum == 0) {
        current_thread->th_quantum = SCHED_DEFAULT_QUANTUM;
        sched_yield();
    }
}

void sched_yield(void)
{
    if (!current_thread)
        return;

    struct thread *next = sched_dequeue();
    if (!next)
        return;  /* no other thread to run */

    struct thread *prev = current_thread;

    /* Put the current thread back in the queue */
    if (prev->th_state != THREAD_STATE_HALTED)
        sched_enqueue(prev);

    current_thread = next;
    thread_switch(prev, next);
}

struct thread *sched_current(void)
{
    return current_thread;
}

void sched_set_current(struct thread *th)
{
    current_thread = th;
    if (th)
        th->th_state = THREAD_STATE_RUNNING;
}
