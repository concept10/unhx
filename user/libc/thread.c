/*
 * user/libc/thread.c — User threading API for UNHOX
 */

#include "thread.h"
#include "syscall.h"

thread_id_t unhx_thread_create(void (*entry)(void *), void *arg,
                                size_t stack_size)
{
    (void)stack_size; /* kernel allocates if stack_ptr == 0 */

    long ret = thread_create_syscall((long)entry, (long)arg, 0);
    if (ret < 0)
        return (thread_id_t)-1;
    return (thread_id_t)ret;
}
