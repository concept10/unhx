/*
 * kernel/platform/irq.h — IRQ management and interrupt control for UNHOX
 *
 * Provides inline helpers for enabling/disabling interrupts and
 * saving/restoring interrupt state (for use in critical sections).
 *
 * Reference: Intel SDM Vol. 3A §6.8 — Interrupt Control.
 */

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/* Forward declaration */
struct interrupt_frame;

/*
 * irq_disable — disable maskable interrupts (cli).
 */
static inline void irq_disable(void)
{
    __asm__ volatile ("cli" ::: "memory");
}

/*
 * irq_enable — enable maskable interrupts (sti).
 */
static inline void irq_enable(void)
{
    __asm__ volatile ("sti" ::: "memory");
}

/*
 * irq_save — save the current interrupt state and disable interrupts.
 * Returns the RFLAGS value before disabling.
 */
static inline uint64_t irq_save(void)
{
    uint64_t flags;
    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
    return flags;
}

/*
 * irq_restore — restore interrupt state from a previous irq_save().
 * Re-enables interrupts only if they were enabled before the save.
 */
static inline void irq_restore(uint64_t flags)
{
    __asm__ volatile (
        "pushq %0\n\t"
        "popfq"
        :
        : "r"(flags)
        : "memory", "cc"
    );
}

/* IRQ handler function type */
typedef void (*irq_handler_fn)(struct interrupt_frame *frame);

/*
 * irq_register — register a handler for a specific IRQ line (0–15).
 */
void irq_register(uint8_t irq, irq_handler_fn handler);

/*
 * irq_handler — C-level IRQ dispatch.
 *
 * Called from isr_dispatch() in idt.c for vectors 0x20–0x2F.
 * Individual IRQ handlers (timer, keyboard, etc.) are registered
 * as the system is brought up.
 */
void irq_handler(uint8_t irq, struct interrupt_frame *frame);

#endif /* IRQ_H */
