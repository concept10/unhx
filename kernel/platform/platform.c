/*
 * kernel/platform/platform.c — x86-64 hardware abstraction initialisation
 *                               and early serial console for UNHOX
 *
 * Implements platform_init(), serial_putchar(), and serial_putstr().
 * The serial port (COM1 at I/O base 0x3F8) is the only output mechanism
 * available before the VGA or framebuffer subsystems are set up.
 *
 * Reference: NS16550A UART specification; QEMU -serial stdio redirects this
 *            port to standard output, making it ideal for early boot messages.
 */

#include "platform.h"
#include "gdt.h"

/* COM1 I/O base address (standard PC) */
#define COM1_PORT   0x3F8

/* I/O port access helpers — x86-64 in/out instructions */
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

/* -------------------------------------------------------------------------
 * Serial port initialisation
 *
 * Programs COM1 for 115200 baud, 8N1, no flow control.
 * ------------------------------------------------------------------------- */

static void serial_init(void)
{
    outb(COM1_PORT + 1, 0x00); /* Disable all interrupts                   */
    outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor)      */
    outb(COM1_PORT + 0, 0x01); /* Divisor low byte:  1 → 115200 baud       */
    outb(COM1_PORT + 1, 0x00); /* Divisor high byte: 0                     */
    outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit (8N1)    */
    outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold    */
    outb(COM1_PORT + 4, 0x0B); /* RTS/DSR set                              */
}

void serial_putchar(char c)
{
    /* Wait until the transmit holding register is empty */
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
        ;
    outb(COM1_PORT, (unsigned char)c);
}

void serial_putstr(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}

/* -------------------------------------------------------------------------
 * platform_init
 * ------------------------------------------------------------------------- */

void platform_init(void)
{
    gdt_init();
    serial_init();
}
