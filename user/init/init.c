/*
 * user/init/init.c — First userspace process for UNHOX
 *
 * This is the "Hello from userspace" milestone (v0.4).
 * Prints a message to the serial console via the SYS_WRITE system call.
 */

#include "../libc/syscall.h"

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

    exit(0);
    return 0;
}
