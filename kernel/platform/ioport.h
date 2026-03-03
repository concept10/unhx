/*
 * kernel/platform/ioport.h — I/O port access functions
 *
 * Basic x86-64 in() and out() instructions for port I/O.
 */

#ifndef IOPORT_H
#define IOPORT_H

#include <stdint.h>

/* Read byte from I/O port */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "d"(port));
    return val;
}

/* Write byte to I/O port */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "d"(port));
}

/* Read word (16-bit) from I/O port */
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "d"(port));
    return val;
}

/* Write word (16-bit) to I/O port */
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "d"(port));
}

/* Read long (32-bit) from I/O port */
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "d"(port));
    return val;
}

/* Write long (32-bit) to I/O port */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "d"(port));
}

#endif /* IOPORT_H */
