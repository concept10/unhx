/*
 * user/libc/assert.h — Assertion macro for UNHOX userspace
 */

#ifndef USER_ASSERT_H
#define USER_ASSERT_H

#include "stdio.h"
#include "syscall.h"

#define assert(cond) do { \
    if (!(cond)) { \
        printf("ASSERT FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

#endif /* USER_ASSERT_H */
