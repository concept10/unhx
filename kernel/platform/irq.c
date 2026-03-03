/*
 * kernel/platform/irq.c — Hardware IRQ dispatch for UNHOX
 *
 * Routes hardware IRQs (0–15) to registered handlers.
 * In Step 2, only a stub is installed.  Step 4 adds the PIT timer handler.
 *
 * Reference: Intel SDM Vol. 3A §6.12 — IRQ Handling.
 */

#include "irq.h"
#include "idt.h"

/* IRQ handler table — one function pointer per IRQ line (0–15) */
static irq_handler_fn irq_handlers[16];

void irq_register(uint8_t irq, irq_handler_fn handler)
{
    if (irq < 16)
        irq_handlers[irq] = handler;
}

void irq_handler(uint8_t irq, struct interrupt_frame *frame)
{
    if (irq < 16 && irq_handlers[irq])
        irq_handlers[irq](frame);
    /* If no handler registered, the IRQ is silently acknowledged (EOI in caller) */
}
