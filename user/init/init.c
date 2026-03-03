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

    long child = fork();
    if (child > 0) {
        /* Parent process: has child task ID */
        const char *fork_ok = "[init] PASS — fork syscall reachable\r\n";
        write(1, fork_ok, my_strlen(fork_ok));

        /* Wait for the child to exit */
        int status = -1;
        long waited = waitpid(child, &status, 0);
        if (waited == child) {
            const char *wait_ok = "[init] PASS — wait syscall reachable\r\n";
            write(1, wait_ok, my_strlen(wait_ok));
        } else {
            const char *wait_fail = "[init] FAIL — wait syscall failed\r\n";
            write(1, wait_fail, my_strlen(wait_fail));
        }
    } else if (child == 0) {
        /* Child process: fork returns 0 in child */
        /* Child exits cleanly; parent's wait() will reap it */
        exit(0);
    } else {
        /* Error case */
        const char *fork_fail = "[init] FAIL — fork syscall failed\r\n";
        write(1, fork_fail, my_strlen(fork_fail));
    }

    const char *exec_hint = "[init] info: run /bin/init.elf in shell to verify exec\r\n";
    write(1, exec_hint, my_strlen(exec_hint));

    /* v0.5: enter the interactive shell (never returns) */
    shell_main();
    return 0;
}
