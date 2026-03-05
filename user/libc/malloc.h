/*
 * user/libc/malloc.h — Minimal heap allocator for UNHOX userspace
 */

#ifndef USER_MALLOC_H
#define USER_MALLOC_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t new_size);
void  free(void *ptr);

#endif /* USER_MALLOC_H */
