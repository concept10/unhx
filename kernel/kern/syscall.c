/*
 * kernel/kern/syscall.c — System call handler for UNHOX
 *
 * Dispatches system calls from ring-3 user programs.
 * User programs invoke `int $0x80` with:
 *   RAX = syscall number
 *   RDI, RSI, RDX = arguments
 *
 * The return value is placed in frame->rax.
 */

#include "syscall.h"
#include "sched.h"
#include "task.h"
#include "thread.h"
#include "platform/idt.h"

extern void serial_putstr(const char *s);
extern void serial_putchar(char c);
extern char serial_getchar(void);

/* -------------------------------------------------------------------------
 * SYS_WRITE — write a string to the serial console.
 *
 * RDI = pointer to buffer (user address)
 * RSI = length in bytes
 *
 * Returns the number of bytes written.
 *
 * TODO: validate user pointer against the task's vm_map.
 * For Phase 2, we trust the user pointer (kernel tasks only initially).
 * ------------------------------------------------------------------------- */

static int64_t sys_write(struct interrupt_frame *frame)
{
    const char *buf = (const char *)frame->rdi;
    uint64_t len = frame->rsi;

    /* Basic safety: reject null or obviously bad pointers */
    if (!buf || len > 0x100000)
        return -1;

    for (uint64_t i = 0; i < len; i++)
        serial_putchar(buf[i]);

    return (int64_t)len;
}

/* -------------------------------------------------------------------------
 * SYS_READ — read a character from the serial console.
 *
 * RDI = pointer to buffer (user address)
 * RSI = max length
 *
 * Returns the number of bytes read (1 on success, 0 if no data).
 * ------------------------------------------------------------------------- */

static int64_t sys_read(struct interrupt_frame *frame)
{
    char *buf = (char *)frame->rdi;
    uint64_t len = frame->rsi;

    if (!buf || len == 0)
        return -1;

    char c = serial_getchar();
    if (c == 0)
        return 0;  /* no data available */

    buf[0] = c;
    return 1;
}

/* -------------------------------------------------------------------------
 * SYS_EXIT — terminate the current task.
 *
 * RDI = exit status (unused for now)
 * ------------------------------------------------------------------------- */

static void sys_exit(struct interrupt_frame *frame)
{
    (void)frame;
    struct thread *th = sched_current();
    if (th) {
        th->th_state = THREAD_STATE_HALTED;
        sched_yield();
    }
    /* Should not reach here */
    for (;;)
        __asm__ volatile ("cli; hlt");
}

/* -------------------------------------------------------------------------
 * syscall_dispatch — main system call dispatcher.
 * ------------------------------------------------------------------------- */

void syscall_dispatch(struct interrupt_frame *frame)
{
    uint64_t syscall_nr = frame->rax;

    switch (syscall_nr) {
    case SYS_WRITE:
        frame->rax = (uint64_t)sys_write(frame);
        break;

    case SYS_READ:
        frame->rax = (uint64_t)sys_read(frame);
        break;

    case SYS_EXIT:
        sys_exit(frame);
        break;

    case SYS_MACH_MSG:
        /* TODO: implement mach_msg syscall */
        frame->rax = (uint64_t)-1;
        break;

    default:
        frame->rax = (uint64_t)-1;
        break;
    }
}
