/*
 * kernel/kern/syscall.c — System call handler for UNHOX
 *
 * Dispatches system calls from ring-3 user programs.
 * User programs invoke `int $0x80` with:
 *   RAX = syscall number
 *   RDI, RSI, RDX, RCX = arguments
 *
 * The return value is placed in frame->rax.
 */

#include "syscall.h"
#include "sched.h"
#include "task.h"
#include "thread.h"
#include "platform/idt.h"
#include "kern.h"
#include "elf.h"
#include "bsd/bsd_msg.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_mqueue.h"
#include "vm/vm_map.h"
#include "klib.h"

extern void serial_putstr(const char *s);
extern void serial_putchar(char c);
extern char serial_getchar(void);

/* User heap base address — above the 0x400000 text/data region */
#define USER_HEAP_BASE  0x600000

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
    int status = (int)frame->rdi;

    bsd_proc_exit_current(status);

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
 * SYS_FORK — fork the current task (Phase 2 implementation).
 *
 * Arguments: (none)
 *
 * Returns: child task ID in parent, 0 in child.
 *
 * Phase 2 Implementation:
 *   - Creates a new task with a copy of the parent's address space
 *   - Creates a user thread in the child that will return with RAX=0
 *   - Parent continues with RAX=child_task_id
 *   - Child is added to the scheduler run queue
 *
 * The child thread is set up so that when first scheduled, it will
 * return from the fork() syscall as if it were a new process.
 * --------- ------- ------------------------------------------------- */

static int64_t sys_fork(struct interrupt_frame *frame)
{
    (void)frame;
    
    struct thread *parent_th = sched_current();
    if (!parent_th)
        return -1;

    struct task *parent = parent_th->th_task;
    if (!parent || !parent->active)
        return -1;

    serial_putstr("[fork] cloning parent task\r\n");

    /* Clone the parent task (copy vm_map and create new ipc_space) */
    struct task *child = task_copy(parent);
    if (!child) {
        serial_putstr("[fork] ERROR: task_copy failed\r\n");
        return -1;
    }

    serial_putstr("[fork] creating child thread\r\n");

    /* Create a user thread in the child task that will return from fork with RAX=0 */
    struct thread *child_th = thread_create_fork_child(child, frame);
    if (!child_th) {
        serial_putstr("[fork] ERROR: thread_create_fork_child failed\r\n");
        return -1;
    }

    serial_putstr("[fork] registering in BSD process table\r\n");

    /* Register the fork in the BSD process table (child starts running naturally) */
    bsd_proc_register_fork(parent->task_id, child->task_id);

    serial_putstr("[fork] returning to parent\r\n");

    /* Return child task ID to parent */
    return (int64_t)child->task_id;
}

/* -------------------------------------------------------------------------
 * SYS_EXEC — load and execute a new ELF binary in the current task.
 *
 * RDI = pointer to path string (kernel memory for now)
 * RSI = argv
 * RDX = envp
 *
 * Returns: -1 on error.  On success, does not return (jumps to new entry).
 *
 * TODO (Phase 3):
 *   - Look up the executable in the VFS server
 *   - Handle interpreter (shebang) scripts
 *   - Set up argc/argv on the new stack
 * --------- ------- ------------------------------------------------- */

static int64_t sys_exec(struct interrupt_frame *frame)
{
    const char *path = (const char *)frame->rdi;
    if (!path || path[0] == '\0') {
        return -1;
    }

    return (int64_t)bsd_exec_current(path, frame);
}

/* -------------------------------------------------------------------------
 * SYS_WAIT — wait for a child task to exit and reap it.
 *
 * RDI = child task ID (<=0 means any child)
 * RSI = status out (pointer to int where exit status is stored)
 *
 * Returns: child task ID on success, -1 on error.
 *
 * Phase 2 implementation is backed by the BSD process table.
 * --------- ------- ------------------------------------------------- */

static int64_t sys_wait(struct interrupt_frame *frame)
{
    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    int64_t wanted_child = (int64_t)frame->rdi;
    int *status_out = (int *)frame->rsi;

    return bsd_proc_wait(th->th_task->task_id, wanted_child, status_out);
}

/* -------------------------------------------------------------------------
 * SYS_MACH_MSG_SEND — send a Mach message to a port.
 *
 * RDI = pointer to message buffer (user address, starts with mach_msg_header_t)
 * RSI = message size in bytes
 * RDX = destination port name (overrides msgh_remote_port in header)
 *
 * Returns 0 on success, -1 on error.
 * ------------------------------------------------------------------------- */

static int64_t sys_mach_msg_send(struct interrupt_frame *frame)
{
    const void *user_msg = (const void *)frame->rdi;
    uint64_t msg_size = frame->rsi;
    mach_port_name_t dest_name = (mach_port_name_t)frame->rdx;

    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    if (!user_msg || msg_size < sizeof(mach_msg_header_t) || msg_size > 1024)
        return -1;

    /* Copy message from userspace into a kernel buffer */
    uint8_t kbuf[1024];
    kmemcpy(kbuf, user_msg, msg_size);

    /* Set the destination port name from the explicit argument */
    mach_msg_header_t *hdr = (mach_msg_header_t *)kbuf;
    hdr->msgh_remote_port = dest_name;
    hdr->msgh_size = (mach_msg_size_t)msg_size;

    kern_return_t kr = mach_msg_send(th->th_task, hdr, (mach_msg_size_t)msg_size);
    return (kr == KERN_SUCCESS) ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * SYS_MACH_MSG_RECV — receive a Mach message from a port (blocking).
 *
 * RDI = port name (must hold RECEIVE right)
 * RSI = pointer to user buffer
 * RDX = buffer size
 *
 * Returns bytes received on success, -1 on error.
 * ------------------------------------------------------------------------- */

static int64_t sys_mach_msg_recv(struct interrupt_frame *frame)
{
    mach_port_name_t port_name = (mach_port_name_t)frame->rdi;
    void *user_buf = (void *)frame->rsi;
    uint64_t buf_size = frame->rdx;

    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    if (!user_buf || buf_size == 0 || buf_size > 1024)
        return -1;

    struct task *task = th->th_task;
    struct ipc_space *space = task->t_ipc_space;
    if (!space)
        return -1;

    /* Look up the port */
    ipc_space_lock(space);
    struct ipc_entry *entry = ipc_space_lookup(space, port_name);
    if (!entry || !(entry->ie_bits & IE_BITS_RECEIVE)) {
        ipc_space_unlock(space);
        return -1;
    }
    struct ipc_port *port = entry->ie_object;
    ipc_space_unlock(space);

    if (!port || !port->ip_messages)
        return -1;

    /* Blocking receive directly on the mqueue */
    uint8_t kbuf[1024];
    mach_msg_size_t out_size = 0;
    mach_msg_return_t mr = ipc_mqueue_receive(port->ip_messages,
                                               kbuf, (mach_msg_size_t)buf_size,
                                               &out_size, 1 /* blocking */);
    if (mr != MACH_MSG_SUCCESS)
        return -1;

    /* Copy to userspace */
    mach_msg_size_t copy_size = out_size;
    if (copy_size > buf_size)
        copy_size = (mach_msg_size_t)buf_size;
    kmemcpy(user_buf, kbuf, copy_size);

    return (int64_t)out_size;
}

/* -------------------------------------------------------------------------
 * SYS_THREAD_CREATE — create a new user thread in the current task.
 *
 * RDI = entry point (user virtual address)
 * RSI = argument (passed as first parameter to entry)
 * RDX = stack pointer (0 = kernel allocates a 16 KB stack)
 *
 * Returns thread ID on success, -1 on error.
 * ------------------------------------------------------------------------- */

static int64_t sys_thread_create(struct interrupt_frame *frame)
{
    uint64_t entry_point = frame->rdi;
    uint64_t arg = frame->rsi;
    uint64_t stack_ptr = frame->rdx;

    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    struct task *task = th->th_task;

    /* If no stack provided, allocate a 16 KB user stack */
    if (stack_ptr == 0) {
        /* Use a high address region for thread stacks */
        static uint64_t next_stack_addr = 0x7F000000;
        uint64_t stack_size = 16 * 1024;
        uint64_t stack_base = next_stack_addr - stack_size;
        next_stack_addr -= stack_size + 4096; /* guard page gap */

        kern_return_t kr = vm_map_enter(task->t_map, stack_base, stack_size,
                                         VM_PROT_READ | VM_PROT_WRITE);
        if (kr != KERN_SUCCESS)
            return -1;

        stack_ptr = stack_base + stack_size; /* stack grows down */
    }

    struct thread *new_th = thread_create_user_with_arg(task, entry_point,
                                                         stack_ptr, arg);
    if (!new_th)
        return -1;

    sched_enqueue(new_th);
    return (int64_t)new_th->th_id;
}

/* -------------------------------------------------------------------------
 * SYS_SBRK — adjust the program break (heap end).
 *
 * RDI = increment in bytes (0 = query current break)
 *
 * Returns the previous break address on success, -1 on error.
 * ------------------------------------------------------------------------- */

static int64_t sys_sbrk(struct interrupt_frame *frame)
{
    int64_t increment = (int64_t)frame->rdi;

    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    struct task *task = th->th_task;

    /* Initialize heap on first use */
    if (task->t_brk == 0) {
        task->t_brk_base = USER_HEAP_BASE;
        task->t_brk = USER_HEAP_BASE;
    }

    uint64_t old_brk = task->t_brk;

    if (increment == 0)
        return (int64_t)old_brk;

    if (increment < 0)
        return -1; /* shrinking not supported */

    /* Round up to page size for vm_map_enter */
    uint64_t alloc_base = old_brk;
    uint64_t alloc_size = ((uint64_t)increment + 4095) & ~4095ULL;

    kern_return_t kr = vm_map_enter(task->t_map, alloc_base, alloc_size,
                                     VM_PROT_READ | VM_PROT_WRITE);
    if (kr != KERN_SUCCESS)
        return -1;

    task->t_brk = alloc_base + alloc_size;
    return (int64_t)old_brk;
}

/* -------------------------------------------------------------------------
 * SYS_PORT_ALLOC — allocate a new Mach port in the current task.
 *
 * No arguments.
 *
 * Returns the port name on success, -1 on error.
 * Grants the caller both SEND and RECEIVE rights.
 * ------------------------------------------------------------------------- */

static int64_t sys_port_alloc(struct interrupt_frame *frame)
{
    (void)frame;

    struct thread *th = sched_current();
    if (!th || !th->th_task)
        return -1;

    struct task *task = th->th_task;

    /* Allocate a kernel port object */
    struct ipc_port *port = ipc_port_alloc(task);
    if (!port)
        return -1;

    /* Allocate a name in the task's IPC space */
    struct ipc_space *space = task->t_ipc_space;
    ipc_space_lock(space);

    mach_port_name_t name;
    kern_return_t kr = ipc_space_alloc_name(space, &name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(space);
        ipc_port_destroy(port);
        return -1;
    }

    /* Install the port with both SEND and RECEIVE rights */
    struct ipc_entry *entry = &space->is_table[name];
    entry->ie_object = port;
    entry->ie_bits = IE_BITS_SEND | IE_BITS_RECEIVE;

    ipc_space_unlock(space);

    return (int64_t)name;
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

    case SYS_FORK:
        frame->rax = (uint64_t)sys_fork(frame);
        break;

    case SYS_EXEC:
        frame->rax = (uint64_t)sys_exec(frame);
        break;

    case SYS_WAIT:
        frame->rax = (uint64_t)sys_wait(frame);
        break;

    case SYS_MACH_MSG:
        frame->rax = (uint64_t)-1; /* reserved for combined send+receive */
        break;

    case SYS_MACH_MSG_SEND:
        frame->rax = (uint64_t)sys_mach_msg_send(frame);
        break;

    case SYS_MACH_MSG_RECV:
        frame->rax = (uint64_t)sys_mach_msg_recv(frame);
        break;

    case SYS_THREAD_CREATE:
        frame->rax = (uint64_t)sys_thread_create(frame);
        break;

    case SYS_SBRK:
        frame->rax = (uint64_t)sys_sbrk(frame);
        break;

    case SYS_PORT_ALLOC:
        frame->rax = (uint64_t)sys_port_alloc(frame);
        break;

    default:
        frame->rax = (uint64_t)-1;
        break;
    }
}
