/*
 * user/libc/syscall.h — System call wrappers for UNHOX userspace
 *
 * These functions invoke the kernel via `int $0x80`.
 * Syscall number in RAX, arguments in RDI, RSI, RDX, RCX.
 */

#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stddef.h>
#include <stdint.h>

#define SYS_MACH_MSG    0
#define SYS_WRITE       1
#define SYS_EXIT        2
#define SYS_READ        3
#define SYS_FORK        4
#define SYS_EXEC        5
#define SYS_WAIT        6

static inline long syscall0(long nr)
{
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(nr)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long nr, long a1)
{
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret)
                      : "a"(nr), "D"(a1)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long write(int fd, const void *buf, size_t count)
{
    (void)fd;
    return syscall2(SYS_WRITE, (long)buf, (long)count);
}

static inline long read(int fd, void *buf, size_t count)
{
    (void)fd;
    return syscall2(SYS_READ, (long)buf, (long)count);
}

static inline long fork(void)
{
    return syscall0(SYS_FORK);
}

static inline long execve(const char *path, char *const argv[], char *const envp[])
{
    return syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}

static inline long waitpid(long pid, int *status, long options)
{
    (void)options;
    return syscall2(SYS_WAIT, pid, (long)status);
}

static inline void exit(int status)
{
    syscall2(SYS_EXIT, (long)status, 0);
    for (;;) ;  /* unreachable */
}

#endif /* USER_SYSCALL_H */
