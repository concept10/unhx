/*
 * user/libc/thread.h — User threading API for UNHOX
 *
 * Creates new threads in the current task via SYS_THREAD_CREATE.
 */

#ifndef USER_THREAD_H
#define USER_THREAD_H

#include <stddef.h>

typedef unsigned long thread_id_t;

/*
 * Create a new thread.  entry receives arg as its first parameter.
 * stack_size of 0 means the kernel allocates a default 16 KB stack.
 * Returns the thread ID on success, (thread_id_t)-1 on error.
 */
thread_id_t unhx_thread_create(void (*entry)(void *), void *arg,
                                size_t stack_size);

#endif /* USER_THREAD_H */
