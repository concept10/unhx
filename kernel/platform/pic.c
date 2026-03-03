/*
 * kernel/platform/pic.c — i8259 PIC driver for UNHOX
 *
 * Remaps the master/slave PIC to vectors 0x20–0x2F and provides
 * EOI, mask, and unmask operations.
 *
 * Reference: Intel 8259A datasheet; OSDev wiki "8259 PIC".
 */

#include "pic.h"

/* I/O port access helpers (same as in platform.c) */
static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Small I/O delay for slow PIC hardware */
static inline void io_wait(void)
{
    outb(0x80, 0);  /* port 0x80 is used for POST codes; safe dummy write */
}

void pic_init(void)
{
    /* Save current masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /*
     * ICW1: begin initialization sequence (cascade mode, ICW4 needed).
     * Bit 0 = IC4 (ICW4 needed)
     * Bit 4 = 1 (initialization command)
     */
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();

    /*
     * ICW2: set vector offsets.
     * Master PIC: IRQ 0–7 → vectors 0x20–0x27
     * Slave PIC:  IRQ 8–15 → vectors 0x28–0x2F
     */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /*
     * ICW3: cascading configuration.
     * Master: slave PIC on IRQ 2 (bit mask 0x04)
     * Slave:  cascade identity = 2
     */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /*
     * ICW4: 8086 mode, normal EOI.
     */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /*
     * Mask all IRQs initially.
     * Individual IRQs will be unmasked as their handlers are registered.
     */
    (void)mask1;
    (void)mask2;
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq)
{
    /* If the IRQ came from the slave PIC (IRQ 8–15), send EOI to both */
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}

void pic_mask(uint8_t irq)
{
    uint16_t port;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    uint8_t mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}
