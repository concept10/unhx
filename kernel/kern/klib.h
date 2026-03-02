/*
 * kernel/kern/klib.h — Minimal C library functions for freestanding UNHU kernel
 *
 * In a freestanding environment (-ffreestanding) the standard C library is not
 * available.  We provide the handful of functions the kernel actually needs.
 */

#ifndef KLIB_H
#define KLIB_H

#include <stddef.h>

void *kmemset(void *s, int c, size_t n);
void *kmemcpy(void *dest, const void *src, size_t n);
int   kmemcmp(const void *s1, const void *s2, size_t n);
size_t kstrlen(const char *s);
int   kstrcmp(const char *s1, const char *s2);
char *kstrncpy(char *dest, const char *src, size_t n);

#endif /* KLIB_H */
