/*
 * user/sh/sh.c — Minimal shell for UNHOX (milestone v0.5)
 *
 * Provides an "unhox$ " prompt on the serial console.
 * Called by init after the v0.4 hello message is printed.
 *
 * Built-in commands:
 *   echo <text>  — print text to serial
 *   help         — list available commands
 *   exit         — terminate the shell
 *
 * Input is read one character at a time via SYS_READ (which polls the
 * serial LSR). The shell busy-spins until a character is available —
 * acceptable for a freestanding microkernel prototype.
 */

#include "../libc/syscall.h"

/* -------------------------------------------------------------------------
 * Internal helpers (no libc available)
 * ------------------------------------------------------------------------- */

static unsigned long sh_strlen(const char *s)
{
    unsigned long n = 0;
    while (s[n])
        n++;
    return n;
}

static void sh_print(const char *s)
{
    write(1, s, sh_strlen(s));
}

static int sh_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int sh_strncmp(const char *a, const char *b, unsigned long n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n ? ((unsigned char)*a - (unsigned char)*b) : 0;
}

/* -------------------------------------------------------------------------
 * readline — read one line from serial, echoing characters.
 *
 * Handles:
 *   '\r' / '\n'  — end of line
 *   '\b' / DEL   — backspace (erase last char)
 *   other ctrl   — silently ignored
 *
 * Returns the number of characters placed in buf (not counting '\0').
 * ------------------------------------------------------------------------- */

static int readline(char *buf, int max)
{
    int n = 0;

    while (n < max - 1) {
        char c = 0;
        long r;

        /* Spin until a character arrives on the serial port */
        do {
            r = read(0, &c, 1);
        } while (r <= 0);

        if (c == '\r' || c == '\n') {
            buf[n] = '\0';
            sh_print("\r\n");
            break;
        }

        /* Backspace / DEL */
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                sh_print("\b \b");
            }
            continue;
        }

        /* Ignore non-printable control characters */
        if ((unsigned char)c < 0x20)
            continue;

        buf[n++] = c;
        write(1, &c, 1);    /* local echo */
    }

    return n;
}

/* -------------------------------------------------------------------------
 * shell_main — entered by init after printing the v0.4 hello message.
 * Never returns.
 * ------------------------------------------------------------------------- */

void shell_main(void)
{
    static char line[256];

    sh_print("\r\nUNHOX Shell v0.5\r\n");
    sh_print("Type 'help' for commands.\r\n\r\n");

    for (;;) {
        sh_print("unhox$ ");
        int len = readline(line, (int)sizeof(line));
        if (len == 0)
            continue;

        /* echo [text] */
        if (sh_strncmp(line, "echo", 4) == 0 &&
            (line[4] == ' ' || line[4] == '\0')) {
            const char *arg = (line[4] == ' ') ? &line[5] : "";
            sh_print(arg);
            sh_print("\r\n");
        }

        /* help */
        else if (sh_strcmp(line, "help") == 0) {
            sh_print("Built-in commands:\r\n");
            sh_print("  echo <text>   print text to serial\r\n");
            sh_print("  help          show this help\r\n");
            sh_print("  exit          exit the shell\r\n");
        }

        /* exit */
        else if (sh_strcmp(line, "exit") == 0) {
            sh_print("Goodbye.\r\n");
            exit(0);
        }

        else {
            sh_print(line);
            sh_print(": command not found\r\n");
        }
    }
}
