/*
 * user/libc/stdio.h — Minimal formatted output for UNHOX userspace
 */

#ifndef USER_STDIO_H
#define USER_STDIO_H

#include <stddef.h>
#include <stdarg.h>

int printf(const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
void putchar(int c);
void puts(const char *s);

#endif /* USER_STDIO_H */
