/*
 * kernel/platform/aarch64/platform.c — AArch64 hardware abstraction for UNHOX
 *
 * Implements platform_init(), serial_putchar(), serial_putstr(), and related
 * helpers using the ARM PL011 UART.  On QEMU's -machine virt the PL011 is
 * mapped at 0x09000000 and routed to stdio via -serial stdio.
 *
 * Reference: ARM PrimeCell UART (PL011) Technical Reference Manual (ARM DDI 0183).
 */

#include "../platform.h"

/* PL011 base address in QEMU -machine virt */
#define PL011_BASE  0x09000000UL

/* PL011 register offsets (byte-addressed; accessed as 32-bit words) */
#define PL011_DR    0x000   /* Data Register (TX write / RX read)            */
#define PL011_FR    0x018   /* Flag Register (status bits)                   */
#define PL011_IBRD  0x024   /* Integer Baud Rate Divisor                     */
#define PL011_FBRD  0x028   /* Fractional Baud Rate Divisor                  */
#define PL011_LCR_H 0x02C   /* Line Control Register High                    */
#define PL011_CR    0x030   /* Control Register                              */

/* Flag Register bits */
#define PL011_FR_TXFF   (1u << 5)   /* Transmit FIFO full                   */
#define PL011_FR_BUSY   (1u << 3)   /* UART transmitter busy                 */

/* Control Register bits */
#define PL011_CR_UARTEN (1u << 0)   /* UART enable                          */
#define PL011_CR_TXE    (1u << 8)   /* Transmit enable                       */
#define PL011_CR_RXE    (1u << 9)   /* Receive enable                        */

/* Line Control bits */
#define PL011_LCR_WLEN8 (3u << 5)   /* 8-bit words                          */
#define PL011_LCR_FEN   (1u << 4)   /* Enable FIFOs                          */

/* ---------------------------------------------------------------------------
 * MMIO helpers
 * ------------------------------------------------------------------------- */

static inline void mmio_write32(unsigned long addr, unsigned int val)
{
    *(volatile unsigned int *)addr = val;
}

static inline unsigned int mmio_read32(unsigned long addr)
{
    return *(volatile unsigned int *)addr;
}

/* ---------------------------------------------------------------------------
 * PL011 initialisation
 *
 * Programs the UART for 115200 baud assuming a 24 MHz reference clock:
 *   BRD = 24 000 000 / (16 × 115 200) ≈ 13.021
 *   IBRD = 13, FBRD = round(0.021 × 64) = 1
 * ------------------------------------------------------------------------- */

static void pl011_init(void)
{
    /* Disable the UART before reconfiguring */
    mmio_write32(PL011_BASE + PL011_CR, 0);

    /* Wait for any in-progress transmission to complete */
    while (mmio_read32(PL011_BASE + PL011_FR) & PL011_FR_BUSY)
        ;

    /* Set baud rate divisors */
    mmio_write32(PL011_BASE + PL011_IBRD, 13);
    mmio_write32(PL011_BASE + PL011_FBRD, 1);

    /* 8N1 with FIFO enabled */
    mmio_write32(PL011_BASE + PL011_LCR_H, PL011_LCR_WLEN8 | PL011_LCR_FEN);

    /* Enable UART, TX, and RX */
    mmio_write32(PL011_BASE + PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

/* ---------------------------------------------------------------------------
 * Public serial output API (matches platform.h)
 * ------------------------------------------------------------------------- */

void serial_putchar(char c)
{
    /* Spin until the transmit FIFO has space */
    while (mmio_read32(PL011_BASE + PL011_FR) & PL011_FR_TXFF)
        ;
    mmio_write32(PL011_BASE + PL011_DR, (unsigned int)(unsigned char)c);
}

void serial_putstr(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}

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

/* ---------------------------------------------------------------------------
 * platform_init — initialise all AArch64 hardware abstractions
 * ------------------------------------------------------------------------- */

void platform_init(void)
{
    pl011_init();
}
