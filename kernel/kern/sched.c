/*
 * kernel/kern/sched.c — Round-robin preemptive scheduler for UNHOX
 *
 * Phase 2: The PIT timer fires IRQ 0 at ~100 Hz.  The interrupt handler
 * calls sched_tick() which decrements the current thread's quantum.
 * When the quantum expires, sched_yield() context-switches to the next
 * runnable thread.
 *
 * The context switch from interrupt context works correctly: the interrupt
 * frame stays on the preempted thread's kernel stack, and when that thread
 * is re-scheduled, the stack unwinds through the ISR back to iretq.
 *
 * Run queue operations are protected by disabling interrupts (irq_save/restore).
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4.3 — Scheduling.
 */

#include "sched.h"
#include "platform/irq.h"
#include "platform/pic.h"
#include "platform/idt.h"

/* Serial output (platform layer) */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);

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
 * PIT base frequency: 1,193,182 Hz
 * Divisor for 100 Hz: 1193182 / 100 = 11932 (0x2E9C)
 * ------------------------------------------------------------------------- */

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND         0x43

static void pit_init(void)
{
    uint16_t divisor = 11932;  /* ~100 Hz */

    /* channel 0, lobyte/hibyte, mode 2 (rate generator), binary */
    outb(PIT_COMMAND, 0x34);
    outb(PIT_CHANNEL0_DATA, (unsigned char)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (unsigned char)((divisor >> 8) & 0xFF));
}

/* -------------------------------------------------------------------------
 * Timer IRQ handler — called from irq_handler() in irq.c
 * ------------------------------------------------------------------------- */

static void timer_irq_handler(struct interrupt_frame *frame)
{
    (void)frame;
    sched_tick();
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

    /* Register timer IRQ handler and unmask IRQ 0 */
    irq_register(IRQ_TIMER, timer_irq_handler);
    pic_unmask(IRQ_TIMER);
}

void sched_enqueue(struct thread *th)
{
    if (!th)
        return;

    uint64_t flags = irq_save();

    th->th_sched_next = (void *)0;

    if (run_queue_tail) {
        run_queue_tail->th_sched_next = th;
    } else {
        run_queue_head = th;
    }
    run_queue_tail = th;

    irq_restore(flags);
}

struct thread *sched_dequeue(void)
{
    uint64_t flags = irq_save();

    if (!run_queue_head) {
        irq_restore(flags);
        return (void *)0;
    }

    struct thread *th = run_queue_head;
    run_queue_head = th->th_sched_next;
    th->th_sched_next = (void *)0;

    if (!run_queue_head)
        run_queue_tail = (void *)0;

    irq_restore(flags);
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
    uint64_t flags = irq_save();

    if (!current_thread) {
        irq_restore(flags);
        return;
    }

    struct thread *next = sched_dequeue();
    if (!next) {
        irq_restore(flags);
        return;  /* no other thread to run */
    }

    struct thread *prev = current_thread;

    /* Re-enqueue the current thread if it's still runnable */
    if (prev->th_state != THREAD_STATE_HALTED &&
        prev->th_state != THREAD_STATE_WAITING)
        sched_enqueue(prev);

    current_thread = next;

    irq_restore(flags);

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

void sched_sleep(void)
{
    uint64_t flags = irq_save();
    if (current_thread)
        current_thread->th_state = THREAD_STATE_WAITING;
    irq_restore(flags);

    /* Yield the CPU.  sched_yield() checks the state and will NOT
     * re-enqueue us because we are WAITING.  We resume here when
     * another thread calls sched_wakeup() on us. */
    sched_yield();
}

void sched_wakeup(struct thread *th)
{
    if (!th)
        return;

    uint64_t flags = irq_save();

    if (th->th_state == THREAD_STATE_WAITING) {
        th->th_state = THREAD_STATE_RUNNABLE;
        sched_enqueue(th);
    }
    irq_restore(flags);
}

void sched_yield_if_waiting(void)
{
    uint64_t flags = irq_save();

    /*
     * If sched_wakeup() already ran (between mqueue_unlock and here),
     * our state is no longer WAITING.  Return immediately so the caller
     * can retry the dequeue — the message is waiting for us.
     */
    if (!current_thread ||
        current_thread->th_state != THREAD_STATE_WAITING) {
        irq_restore(flags);
        return;
    }

    struct thread *next = sched_dequeue();
    if (!next) {
        /*
         * No other runnable thread.  Spin with interrupts enabled until
         * sched_wakeup() changes our state.  The timer ISR may preempt
         * us here and switch to a newly-runnable thread via sched_yield();
         * when we are eventually rescheduled, the while-condition is
         * re-evaluated.
         */
        irq_restore(flags);
        while (current_thread->th_state == THREAD_STATE_WAITING)
            __asm__ volatile ("hlt");
        return;
    }

    /* Switch to the next runnable thread.  Do NOT re-enqueue ourselves —
     * we are WAITING.  sched_wakeup() will enqueue us when a message
     * arrives, and the scheduler will eventually resume us here. */
    struct thread *prev = current_thread;
    current_thread = next;
    irq_restore(flags);
    thread_switch(prev, next);
}

void sched_run(void)
{
    serial_putstr("[UNHOX] enabling interrupts, entering scheduler\r\n");
    irq_enable();

    /* Idle loop: halt until an interrupt fires */
    for (;;)
        __asm__ volatile ("hlt");
}
