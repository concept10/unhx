/*
 * user/init/init.c — First userspace process for UNHOX
 *
 * Milestone v0.4: prints "Hello from userspace" (CI check).
 * Milestone v0.5: calls shell_main() to enter the interactive shell loop.
 */

#include "../libc/syscall.h"

/* Defined in user/sh/sh.c */
extern void shell_main(void);

static unsigned long my_strlen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

int main(void)
{
    const char *msg = "Hello from userspace!\r\n";
    write(1, msg, my_strlen(msg));

    const char *done = "[init] PASS — userspace task running\r\n";
    write(1, done, my_strlen(done));

    /* v0.5: enter the interactive shell (never returns) */
    shell_main();
    return 0;
}
