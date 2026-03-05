/*
 * user/libc/stdio.c — Minimal formatted output for UNHOX userspace
 *
 * printf writes to serial via SYS_WRITE.
 * Supports: %s %d %u %x %lx %ld %lu %p %c %%
 * Width and zero-padding supported (e.g. %08x).
 */

#include "stdio.h"
#include "string.h"
#include "syscall.h"

void putchar(int c)
{
    char ch = (char)c;
    write(1, &ch, 1);
}

void puts(const char *s)
{
    write(1, s, strlen(s));
    putchar('\n');
}

static void out_char(char *buf, size_t size, size_t *pos, char c)
{
    if (*pos < size - 1)
        buf[*pos] = c;
    (*pos)++;
}

static void out_string(char *buf, size_t size, size_t *pos, const char *s)
{
    while (*s)
        out_char(buf, size, pos, *s++);
}

static void out_uint(char *buf, size_t size, size_t *pos,
                     unsigned long val, int base, int width, char pad)
{
    char tmp[24];
    int len = 0;
    const char *digits = "0123456789abcdef";

    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val) {
            tmp[len++] = digits[val % base];
            val /= base;
        }
    }

    /* Pad */
    for (int i = len; i < width; i++)
        out_char(buf, size, pos, pad);

    /* Output digits in reverse */
    for (int i = len - 1; i >= 0; i--)
        out_char(buf, size, pos, tmp[i]);
}

static void out_int(char *buf, size_t size, size_t *pos,
                    long val, int width, char pad)
{
    if (val < 0) {
        out_char(buf, size, pos, '-');
        if (width > 0) width--;
        out_uint(buf, size, pos, (unsigned long)(-val), 10, width, pad);
    } else {
        out_uint(buf, size, pos, (unsigned long)val, 10, width, pad);
    }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t pos = 0;

    if (size == 0)
        return 0;

    while (*fmt) {
        if (*fmt != '%') {
            out_char(buf, size, &pos, *fmt++);
            continue;
        }
        fmt++; /* skip '%' */

        /* Parse width and pad character */
        char pad = ' ';
        int width = 0;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            out_string(buf, size, &pos, s ? s : "(null)");
            break;
        }
        case 'd': {
            long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            out_int(buf, size, &pos, val, width, pad);
            break;
        }
        case 'u': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            out_uint(buf, size, &pos, val, 10, width, pad);
            break;
        }
        case 'x': {
            unsigned long val = is_long ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            out_uint(buf, size, &pos, val, 16, width, pad);
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            out_string(buf, size, &pos, "0x");
            out_uint(buf, size, &pos, (unsigned long)ptr, 16, 0, '0');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            out_char(buf, size, &pos, c);
            break;
        }
        case '%':
            out_char(buf, size, &pos, '%');
            break;
        default:
            out_char(buf, size, &pos, '%');
            out_char(buf, size, &pos, *fmt);
            break;
        }
        fmt++;
    }

    /* Null-terminate */
    if (pos < size)
        buf[pos] = '\0';
    else
        buf[size - 1] = '\0';

    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len > 0) {
        size_t wlen = (size_t)len;
        if (wlen > sizeof(buf) - 1)
            wlen = sizeof(buf) - 1;
        write(1, buf, wlen);
    }
    return len;
}
