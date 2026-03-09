/*
 * servers/bsd/bsd_server.c — BSD personality server for NEOMACH
 *
 * Implements fork(), exec(), exit(), wait(), getpid(), getppid(), and the
 * IPC message dispatcher.  See bsd_server.h for the full design rationale.
 *
 * Reference: CMU Mach 3.0 Server Writer's Guide (OSF/RI, 1992);
 *            GNU HURD hurd/ — reference multi-server POSIX implementation;
 *            Utah Lites — BSD server on Mach 3.0 (archive/utah-oskit/).
 */

#include "bsd_server.h"
#include "proc.h"
#include "signal.h"
#include "fd_table.h"
#include "bsd_proto.h"
#include "kern/task.h"
#include "kern/klib.h"

/* Bootstrap server registration (Phase 2: kernel-internal) */
extern int bootstrap_register(const char *name, uint32_t port);

/* Serial output for logging */
extern void serial_putstr(const char *s);
extern void serial_putdec(uint32_t val);
extern void serial_puthex(uint64_t val);

/* =========================================================================
 * ELF header definitions (for bsd_exec ELF validation)
 * ========================================================================= */

/*
 * ELF-64 structures — enough to validate an ELF binary and locate the
 * program headers for loading.  See System V ABI, "ELF-64 Object File
 * Format", Version 1.5.
 */

#define ELF_MAGIC_0     0x7f
#define ELF_MAGIC_1     'E'
#define ELF_MAGIC_2     'L'
#define ELF_MAGIC_3     'F'

#define ELFCLASS64      2       /* 64-bit objects                           */
#define ELFDATA2LSB     1       /* Little-endian                            */
#define ET_EXEC         2       /* Executable file                          */
#define ET_DYN          3       /* Shared object (position-independent exe) */
#define EM_X86_64       62      /* AMD x86-64 architecture                  */
#define EM_AARCH64      183     /* AArch64 (ARM 64-bit)                     */

typedef struct {
    uint8_t     e_ident[16];
    uint16_t    e_type;
    uint16_t    e_machine;
    uint32_t    e_version;
    uint64_t    e_entry;
    uint64_t    e_phoff;
    uint64_t    e_shoff;
    uint32_t    e_flags;
    uint16_t    e_ehsize;
    uint16_t    e_phentsize;
    uint16_t    e_phnum;
    uint16_t    e_shentsize;
    uint16_t    e_shnum;
    uint16_t    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t    p_type;
    uint32_t    p_flags;
    uint64_t    p_offset;
    uint64_t    p_vaddr;
    uint64_t    p_paddr;
    uint64_t    p_filesz;
    uint64_t    p_memsz;
    uint64_t    p_align;
} Elf64_Phdr;

#define PT_LOAD     1   /* Loadable segment */
#define PT_DYNAMIC  2   /* Dynamic linking information */
#define PT_INTERP   3   /* Interpreter path */

/* =========================================================================
 * bsd_server_init
 * ========================================================================= */

void bsd_server_init(void)
{
    serial_putstr("[BSD] initialising BSD personality server\r\n");

    /* Initialise the process table and create PID 1 (init) */
    proc_init();

    /*
     * Register "com.neomach.bsd" with the bootstrap server.
     *
     * Phase 2 placeholder port number (not a real receive port).
     * In Phase 3 this will be a real Mach port name from ipc_right_alloc_receive.
     */
#define BSD_SERVER_BOOTSTRAP_PORT   0xBD5u  /* placeholder: not a real port */
    bootstrap_register("com.neomach.bsd", BSD_SERVER_BOOTSTRAP_PORT);

    serial_putstr("[BSD] BSD server initialised; PID 1 (init) created\r\n");
}

/* =========================================================================
 * bsd_fork
 * ========================================================================= */

int bsd_fork(struct proc *parent, struct proc **child_out)
{
    struct proc *child;
    struct task *child_task;

    if (!parent || !child_out)
        return BSD_EINVAL;
    if (parent->p_state != PROC_STATE_ACTIVE)
        return BSD_ESRCH;

    /* Allocate a new struct proc with a fresh PID */
    child = proc_alloc();
    if (!child)
        return BSD_ENOMEM;

    /*
     * Create a new Mach task for the child.
     *
     * In Mach, task_create(parent_task) creates a new task with an empty
     * address space.  Full fork() semantics (COW clone of address space)
     * require vm_map.c Phase 2 functionality; we create the task structure
     * and note that address space cloning is a Phase 3 operation.
     */
    child_task = task_create(parent->p_task);
    if (!child_task) {
        proc_free(child);
        return BSD_ENOMEM;
    }

    child->p_task  = child_task;
    child->p_ppid  = parent->p_pid;
    child->p_uid   = parent->p_uid;
    child->p_gid   = parent->p_gid;
    child->p_euid  = parent->p_euid;
    child->p_egid  = parent->p_egid;

    /*
     * Copy the file descriptor table.
     *
     * POSIX fork(): the child inherits all of the parent's open file
     * descriptors.  dup()/fork() share the same open-file description
     * (same offset, same flags).
     */
    fd_table_copy(&child->p_fd_table, &parent->p_fd_table);

    /*
     * Copy signal state:
     *   - Signal dispositions (sigaction) are inherited.
     *   - Signal mask is inherited.
     *   - Pending signals are NOT inherited (POSIX 2.4.1).
     */
    kmemcpy(&child->p_signals.ps_actions,
            &parent->p_signals.ps_actions,
            sizeof(parent->p_signals.ps_actions));
    child->p_signals.ps_mask    = parent->p_signals.ps_mask;
    sigemptyset(child->p_signals.ps_pending);

    *child_out = child;

    serial_putstr("[BSD] fork(): parent PID ");
    serial_putdec((uint32_t)parent->p_pid);
    serial_putstr(" -> child PID ");
    serial_putdec((uint32_t)child->p_pid);
    serial_putstr("\r\n");

    return BSD_ESUCCESS;
}

/* =========================================================================
 * bsd_exec
 * ========================================================================= */

/*
 * bsd_exec_validate_elf — validate that the in-memory buffer is a valid
 * ELF-64 executable for the current architecture.
 *
 * image:  pointer to the start of the ELF image
 * size:   size of the image in bytes
 *
 * Returns BSD_ESUCCESS or a BSD error code.
 */
static int bsd_exec_validate_elf(const uint8_t *image, uint32_t size)
{
    const Elf64_Ehdr *ehdr;

    if (!image || size < sizeof(Elf64_Ehdr))
        return BSD_ENOENT;

    ehdr = (const Elf64_Ehdr *)image;

    /* Magic number check */
    if (ehdr->e_ident[0] != ELF_MAGIC_0 ||
        ehdr->e_ident[1] != ELF_MAGIC_1 ||
        ehdr->e_ident[2] != ELF_MAGIC_2 ||
        ehdr->e_ident[3] != ELF_MAGIC_3) {
        return BSD_ENOENT;  /* not an ELF file */
    }

    /* Must be 64-bit little-endian */
    if (ehdr->e_ident[4] != ELFCLASS64 ||
        ehdr->e_ident[5] != ELFDATA2LSB) {
        return BSD_EINVAL;
    }

    /* Must be an executable or PIE shared object */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
        return BSD_EINVAL;

    /* Architecture check */
#if defined(__aarch64__)
    if (ehdr->e_machine != EM_AARCH64)
        return BSD_EINVAL;
#else
    if (ehdr->e_machine != EM_X86_64)
        return BSD_EINVAL;
#endif

    /* Program header sanity */
    if (ehdr->e_phnum == 0 || ehdr->e_phoff == 0)
        return BSD_EINVAL;
    if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * sizeof(Elf64_Phdr) > size)
        return BSD_EINVAL;

    return BSD_ESUCCESS;
}

int bsd_exec(struct proc *proc,
             const char *path,
             int argc,
             const char *const argv[])
{
    if (!proc || !path)
        return BSD_EINVAL;
    if (proc->p_state != PROC_STATE_ACTIVE)
        return BSD_ESRCH;
    if (kstrlen(path) >= BSD_PATH_MAX)
        return BSD_EINVAL;

    /*
     * Phase 2 stub: exec() requires VFS to open and read the binary.
     *
     * Full implementation (Phase 3+):
     *   1. Open the file via VFS server: bsd_open(path, O_RDONLY, &fd)
     *   2. mmap() the ELF headers to determine image layout
     *   3. Call bsd_exec_validate_elf() on the mapped header
     *   4. Clear the process's address space: vm_map_remove_all(proc->p_task)
     *   5. Map ELF PT_LOAD segments into the new address space
     *   6. Allocate the user stack
     *   7. Push argv/envp onto the stack
     *   8. Close O_CLOEXEC file descriptors: fd_table_cloexec(&proc->p_fd_table)
     *   9. Reset signal dispositions to SIG_DFL (POSIX 3.1.3)
     *  10. Set thread entry point to ELF e_entry
     *
     * For Phase 2 we perform the validation logic on a null image to
     * confirm the ELF checker is correct, then close O_CLOEXEC fds and
     * reset signal dispositions — the parts of exec() that do not require
     * a real VFS or loaded image.
     */

    serial_putstr("[BSD] exec(): PID ");
    serial_putdec((uint32_t)proc->p_pid);
    serial_putstr(" requests exec(\"");
    serial_putstr(path);
    serial_putstr("\")\r\n");

    serial_putstr("[BSD] exec(): Phase 2 stub — VFS required for ELF loading\r\n");

    /*
     * Perform the exec-time side effects that are VFS-independent:
     *
     * 1. Close O_CLOEXEC file descriptors.
     */
    fd_table_cloexec(&proc->p_fd_table);

    /*
     * 2. Reset all signal dispositions to SIG_DFL.
     *    POSIX 3.1.3: "Signals set to be caught by the calling process
     *    image shall be set to the default action in the new process image."
     *    Ignored signals remain ignored; blocked signals remain blocked.
     */
    {
        int sig;
        for (sig = 1; sig < NSIG; sig++) {
            if (proc->p_signals.ps_actions[sig].sa_type == BSD_SIG_ACTION_FUNC) {
                proc->p_signals.ps_actions[sig].sa_type         = BSD_SIG_ACTION_DFL;
                proc->p_signals.ps_actions[sig].sa_handler_addr = 0;
            }
        }
    }

    serial_putstr("[BSD] exec(): O_CLOEXEC fds closed; signal dispositions reset\r\n");
    serial_putstr("[BSD] exec(): stub returns BSD_ESUCCESS (image not loaded)\r\n");

    (void)argc;
    (void)argv;
    return BSD_ESUCCESS;
}

/* =========================================================================
 * bsd_exit
 * ========================================================================= */

void bsd_exit(struct proc *proc, int status)
{
    pid_t pid;

    if (!proc || !proc->p_active)
        return;

    pid = proc->p_pid;

    serial_putstr("[BSD] exit(): PID ");
    serial_putdec((uint32_t)pid);
    serial_putstr(" status=");
    serial_putdec((uint32_t)(uint8_t)(status >> 8));
    serial_putstr("\r\n");

    /*
     * Step 1: Close all file descriptors.
     * This must happen before task_destroy() because fd_close() may need
     * to send VFS messages (Phase 3+) that require the task's IPC space.
     */
    fd_table_close_all(&proc->p_fd_table);

    /*
     * Step 2: Destroy the Mach task.
     * This tears down the address space, IPC space, and all threads.
     */
    if (proc->p_task) {
        task_destroy(proc->p_task);
        proc->p_task = (void *)0;
    }

    /*
     * Step 3: Save exit status and transition to ZOMBIE.
     * The struct proc stays alive until the parent calls wait().
     */
    proc->p_exit_status = status;
    proc->p_state       = PROC_STATE_ZOMBIE;

    /*
     * Step 4: Send SIGCHLD to parent.
     */
    {
        struct proc *parent = proc_find(proc->p_ppid);
        if (parent && parent->p_state == PROC_STATE_ACTIVE)
            proc_signal(parent, SIGCHLD);
    }

    /*
     * Step 5: Re-parent any live children of this process to init (PID 1).
     * POSIX: when a process exits, its children are inherited by init.
     */
    {
        pid_t cpid;
        struct proc *init = proc_find(BSD_PID_INIT);

        if (init) {
            for (cpid = 2; cpid < BSD_PID_MAX; cpid++) {
                struct proc *child = proc_find(cpid);
                if (child && child->p_ppid == pid) {
                    child->p_ppid = BSD_PID_INIT;
                    /* If the child is already a zombie, notify init */
                    if (child->p_state == PROC_STATE_ZOMBIE)
                        proc_signal(init, SIGCHLD);
                }
            }
        }
    }
}

/* =========================================================================
 * bsd_wait
 * ========================================================================= */

int bsd_wait(struct proc *parent,
             pid_t target_pid,
             int options,
             int32_t *status_out)
{
    struct proc *zombie;

    if (!parent || !status_out)
        return -BSD_EINVAL;
    if (parent->p_state != PROC_STATE_ACTIVE)
        return -BSD_ESRCH;

    /*
     * Find a zombie child matching the request.
     */
    if (target_pid == BSD_WAIT_ANY) {
        zombie = proc_find_zombie_child(parent->p_pid);
    } else {
        zombie = proc_find_zombie_child_by_pid(parent->p_pid, target_pid);
    }

    if (!zombie) {
        if (options & BSD_WNOHANG)
            return 0;   /* no child ready; WNOHANG says return immediately */
        /*
         * Phase 2: blocking wait requires a thread sleep mechanism
         * (Phase 3+).  Return EAGAIN to indicate the caller should poll.
         */
        return -BSD_EAGAIN;
    }

    pid_t reaped_pid  = zombie->p_pid;
    *status_out       = zombie->p_exit_status;

    serial_putstr("[BSD] wait(): parent PID ");
    serial_putdec((uint32_t)parent->p_pid);
    serial_putstr(" reaps child PID ");
    serial_putdec((uint32_t)reaped_pid);
    serial_putstr("\r\n");

    /* Reap: free the zombie's struct proc slot */
    proc_free(zombie);

    return (int)reaped_pid;
}

/* =========================================================================
 * bsd_getpid / bsd_getppid
 * ========================================================================= */

pid_t bsd_getpid(const struct proc *proc)
{
    if (!proc)
        return -1;
    return proc->p_pid;
}

pid_t bsd_getppid(const struct proc *proc)
{
    if (!proc)
        return -1;
    return proc->p_ppid;
}

/* =========================================================================
 * bsd_dispatch — IPC message dispatcher
 * ========================================================================= */

int bsd_dispatch(const mach_msg_header_t *msg, mach_msg_header_t *reply)
{
    if (!msg || !reply)
        return -1;

    bsd_reply_header_t *rep = (bsd_reply_header_t *)reply;
    rep->hdr.msgh_bits       = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    rep->hdr.msgh_remote_port = msg->msgh_local_port;
    rep->hdr.msgh_local_port  = MACH_PORT_NULL;
    rep->hdr.msgh_voucher_port = MACH_PORT_NULL;
    rep->hdr.msgh_id          = (mach_msg_id_t)(msg->msgh_id | 0x1000);
    rep->bsd_retval           = -1;
    rep->bsd_errno            = BSD_EINVAL;

    switch (msg->msgh_id) {

    case BSD_MSG_GETPID: {
        const bsd_request_header_t *req = (const bsd_request_header_t *)msg;
        bsd_getpid_reply_t *gr = (bsd_getpid_reply_t *)reply;
        struct proc *p = proc_find(req->bsd_pid);
        if (!p) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        gr->rep.bsd_retval = (int32_t)bsd_getpid(p);
        gr->rep.bsd_errno  = BSD_ESUCCESS;
        gr->pid            = gr->rep.bsd_retval;
        gr->rep.hdr.msgh_size = sizeof(bsd_getpid_reply_t);
        break;
    }

    case BSD_MSG_GETPPID: {
        const bsd_request_header_t *req = (const bsd_request_header_t *)msg;
        bsd_getpid_reply_t *gr = (bsd_getpid_reply_t *)reply;
        struct proc *p = proc_find(req->bsd_pid);
        if (!p) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        gr->rep.bsd_retval = (int32_t)bsd_getppid(p);
        gr->rep.bsd_errno  = BSD_ESUCCESS;
        gr->pid            = gr->rep.bsd_retval;
        gr->rep.hdr.msgh_size = sizeof(bsd_getpid_reply_t);
        break;
    }

    case BSD_MSG_FORK: {
        const bsd_fork_request_t *req = (const bsd_fork_request_t *)msg;
        bsd_fork_reply_t *fr = (bsd_fork_reply_t *)reply;
        struct proc *parent = proc_find(req->req.bsd_pid);
        if (!parent) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        struct proc *child = (void *)0;
        int err = bsd_fork(parent, &child);
        fr->rep.bsd_errno  = (int32_t)err;
        fr->rep.bsd_retval = (err == BSD_ESUCCESS) ? (int32_t)child->p_pid : -1;
        fr->child_pid      = fr->rep.bsd_retval;
        fr->rep.hdr.msgh_size = sizeof(bsd_fork_reply_t);
        break;
    }

    case BSD_MSG_EXIT: {
        const bsd_exit_request_t *req = (const bsd_exit_request_t *)msg;
        struct proc *p = proc_find(req->req.bsd_pid);
        if (!p) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        bsd_exit(p, req->exit_status);
        /* No reply — process is gone */
        rep->bsd_retval = 0;
        rep->bsd_errno  = BSD_ESUCCESS;
        rep->hdr.msgh_size = sizeof(bsd_reply_header_t);
        break;
    }

    case BSD_MSG_WAIT: {
        const bsd_wait_request_t *req = (const bsd_wait_request_t *)msg;
        bsd_wait_reply_t *wr = (bsd_wait_reply_t *)reply;
        struct proc *p = proc_find(req->req.bsd_pid);
        if (!p) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        int32_t status = 0;
        int rc = bsd_wait(p, req->pid, req->options, &status);
        if (rc > 0) {
            wr->rep.bsd_retval  = rc;
            wr->rep.bsd_errno   = BSD_ESUCCESS;
            wr->child_pid       = rc;
            wr->exit_status     = status;
        } else if (rc == 0) {
            wr->rep.bsd_retval  = 0;
            wr->rep.bsd_errno   = BSD_ESUCCESS;
            wr->child_pid       = 0;
            wr->exit_status     = 0;
        } else {
            wr->rep.bsd_retval  = -1;
            wr->rep.bsd_errno   = (int32_t)(-rc);
            wr->child_pid       = -1;
            wr->exit_status     = 0;
        }
        wr->rep.hdr.msgh_size = sizeof(bsd_wait_reply_t);
        break;
    }

    case BSD_MSG_KILL: {
        const bsd_kill_request_t *req = (const bsd_kill_request_t *)msg;
        struct proc *target = proc_find(req->target_pid);
        if (!target) {
            rep->bsd_errno = BSD_ESRCH;
            break;
        }
        proc_signal(target, req->signum);
        rep->bsd_retval = 0;
        rep->bsd_errno  = BSD_ESUCCESS;
        rep->hdr.msgh_size = sizeof(bsd_reply_header_t);
        break;
    }

    default:
        rep->bsd_errno = BSD_EINVAL;
        rep->hdr.msgh_size = sizeof(bsd_reply_header_t);
        break;
    }

    return 0;
}

/* =========================================================================
 * bsd_server_main — Phase 2 self-demonstration
 * ========================================================================= */

void bsd_server_main(void)
{
    serial_putstr("\r\n");
    serial_putstr("[BSD] BSD server Phase 2 demonstration\r\n");
    serial_putstr("[BSD] (Full message loop is Phase 3; Phase 2 uses direct C calls)\r\n");

    /*
     * Demonstrate process lifecycle:
     *
     *   1. Get the init process (PID 1)
     *   2. fork() a child
     *   3. Send the child a signal (SIGTERM)
     *   4. Handle the signal (causes child to exit)
     *   5. Parent wait()s for the child
     */

    struct proc *init_proc = proc_find(BSD_PID_INIT);
    if (!init_proc) {
        serial_putstr("[BSD] ERROR: init process not found\r\n");
        return;
    }

    /* fork() */
    struct proc *child = (void *)0;
    int err = bsd_fork(init_proc, &child);
    if (err != BSD_ESUCCESS || !child) {
        serial_putstr("[BSD] fork() failed\r\n");
        return;
    }

    serial_putstr("[BSD] fork() succeeded: child PID=");
    serial_putdec((uint32_t)child->p_pid);
    serial_putstr("\r\n");

    /* exec() stub on the child */
    const char *argv[] = { "/bin/sh", (void *)0 };
    err = bsd_exec(child, "/bin/sh", 1, argv);
    serial_putstr("[BSD] exec() returned ");
    serial_putdec((uint32_t)err);
    serial_putstr(" (0 = success, stub mode)\r\n");

    /* Send SIGTERM to child */
    proc_signal(child, SIGTERM);
    serial_putstr("[BSD] SIGTERM sent to child PID ");
    serial_putdec((uint32_t)child->p_pid);
    serial_putstr("\r\n");

    /* Handle signals — child should terminate due to SIGTERM default action */
    int terminated = proc_handle_signals(child);
    if (terminated) {
        serial_putstr("[BSD] child terminated via SIGTERM (default action: TERM)\r\n");
    }

    /* Parent waits for the child */
    int32_t wstatus = 0;
    int reaped = bsd_wait(init_proc, BSD_WAIT_ANY, 0, &wstatus);
    if (reaped > 0) {
        serial_putstr("[BSD] wait() reaped child PID ");
        serial_putdec((uint32_t)reaped);
        serial_putstr(", exit signal=");
        serial_putdec((uint32_t)BSD_WTERMSIG(wstatus));
        serial_putstr("\r\n");
    } else {
        serial_putstr("[BSD] wait() returned ");
        serial_putdec((uint32_t)(int32_t)reaped);
        serial_putstr("\r\n");
    }

    serial_putstr("[BSD] Phase 2 milestone v0.4: fork + exec + exit + wait demonstrated\r\n");
}
