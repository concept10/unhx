/*
 * kernel/platform/pic.h — i8259 Programmable Interrupt Controller for UNHOX
 *
 * The legacy 8259 PIC is the standard interrupt controller on PC-compatible
 * hardware and QEMU's default.  Two PICs are cascaded (master + slave),
 * giving 15 usable IRQ lines (IRQ 2 is used for cascading).
 *
 * By default the BIOS maps:
 *   Master PIC: IRQ 0–7  → vectors 0x08–0x0F (CONFLICTS with CPU exceptions!)
 *   Slave PIC:  IRQ 8–15 → vectors 0x70–0x77
 *
 * We remap them to:
 *   Master PIC: IRQ 0–7  → vectors 0x20–0x27
 *   Slave PIC:  IRQ 8–15 → vectors 0x28–0x2F
 *
 * Reference: Intel 8259A datasheet; OSDev wiki "8259 PIC".
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* PIC I/O ports */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

/* IRQ numbers */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4

/* End-Of-Interrupt command */
#define PIC_EOI         0x20

/*
 * pic_init — remap the PIC and mask all IRQs except those we enable.
 */
void pic_init(void);

/*
 * pic_eoi — send End-Of-Interrupt to the appropriate PIC(s).
 * Must be called at the end of every IRQ handler.
 * irq: the IRQ number (0–15).
 */
void pic_eoi(uint8_t irq);

/*
 * pic_unmask — enable a specific IRQ line.
 */
void pic_unmask(uint8_t irq);

/*
 * pic_mask — disable a specific IRQ line.
 */
void pic_mask(uint8_t irq);

#endif /* PIC_H */
