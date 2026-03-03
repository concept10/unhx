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
#include "idt.h"
#include "pic.h"

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

char serial_getchar(void)
{
    /* Check if data is available (bit 0 of LSR) */
    if ((inb(COM1_PORT + 5) & 0x01) == 0)
        return 0;  /* no data */
    return (char)inb(COM1_PORT);
}

void serial_putstr(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}

/* -------------------------------------------------------------------------
 * Hex and decimal output for debugging
 * ------------------------------------------------------------------------- */

void serial_puthex(uint64_t val)
{
    static const char hex[] = "0123456789abcdef";
    serial_putchar('0');
    serial_putchar('x');
    for (int i = 60; i >= 0; i -= 4)
        serial_putchar(hex[(val >> i) & 0xF]);
}

void serial_putdec(uint32_t val)
{
    if (val == 0) {
        serial_putchar('0');
        return;
    }
    char buf[10];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (char)(val % 10);
        val /= 10;
    }
    while (--i >= 0)
        serial_putchar(buf[i]);
}

/* -------------------------------------------------------------------------
 * platform_init
 * ------------------------------------------------------------------------- */

void platform_init(void)
{
    gdt_init();
    serial_init();
    pic_init();
    idt_init();
}
