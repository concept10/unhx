/*
 * servers/bsd/bsd_server.h — BSD personality server public interface for NEOMACH
 *
 * The BSD server (also called the "POSIX personality server") provides a
 * traditional UNIX process model on top of the Mach microkernel.  It
 * implements the semantics of fork(), exec(), exit(), wait(), signals, and
 * file descriptors without placing any BSD code inside the kernel itself.
 *
 * This is the fundamental design of GNU HURD, NeXTSTEP, and OSF/1 —
 * POSIX semantics implemented as a userspace server communicating with the
 * kernel through Mach IPC.
 *
 * PHASE 2 ARCHITECTURE:
 *
 *   In Phase 2 the BSD server is linked into the kernel image and called
 *   directly via C function calls, just like the bootstrap server in Phase 1.
 *   This lets us develop and test the POSIX logic without a working userspace
 *   task switcher or ELF loader.
 *
 *   In Phase 3+ the BSD server will be a true userspace task that:
 *     1. Registers "com.neomach.bsd" with the bootstrap server
 *     2. Receives client requests via mach_msg()
 *     3. Dispatches them to the handler functions defined here
 *     4. Replies via mach_msg() with results
 *
 * SYSCALL EMULATION FLOW:
 *
 *   Application                 BSD Server               Mach Kernel
 *       |                           |                         |
 *       | mach_msg(BSD_MSG_FORK)    |                         |
 *       |──────────────────────────►|                         |
 *       |                           | task_create()           |
 *       |                           |────────────────────────►|
 *       |                           |◄────────────────────────|
 *       |                           | (copies fd table,       |
 *       |                           |  signal state, etc.)    |
 *       | mach_msg(BSD_RPL_FORK)    |                         |
 *       |◄──────────────────────────|                         |
 *       |  child_pid               |                         |
 *
 * Reference: CMU Mach 3.0 Server Writer's Guide (OSF/RI, 1992);
 *            GNU HURD hurd/ — reference multi-server POSIX implementation;
 *            Utah Lites — BSD server on Mach 3.0 (archive/utah-oskit/).
 */

#ifndef BSD_SERVER_H
#define BSD_SERVER_H

#include "proc.h"
#include "signal.h"
#include "fd_table.h"
#include "bsd_proto.h"

/* =========================================================================
 * Initialisation
 * ========================================================================= */

/*
 * bsd_server_init — initialise the BSD personality server.
 *
 * Initialises the process table, creates the init process (PID 1),
 * and registers "com.neomach.bsd" with the bootstrap server.
 *
 * Must be called after kern_init() and bootstrap_main().
 */
void bsd_server_init(void);

/*
 * bsd_server_main — the BSD server's main message dispatch loop.
 *
 * In Phase 3+ this becomes an infinite loop receiving Mach messages on the
 * BSD server port and dispatching them to the handlers below.
 *
 * In Phase 2 this is called once after bsd_server_init() and performs a
 * self-test sequence to demonstrate that the BSD server logic is correct.
 */
void bsd_server_main(void);

/* =========================================================================
 * Process lifecycle
 * ========================================================================= */

/*
 * bsd_fork — fork a process: create a child as a copy of parent.
 *
 * Creates a new Mach task, copies the parent's file descriptor table and
 * signal state into the child, and inserts the child into the process table.
 *
 * parent:      the process requesting fork()
 * child_out:   OUT — pointer to the new child's struct proc
 *
 * Returns 0 on success, a BSD errno on failure.
 *
 * After a successful fork:
 *   - child->p_ppid == parent->p_pid
 *   - child->p_pid  is a fresh PID
 *   - The child's p_fd_table is a copy of the parent's
 *   - The child's p_signals.ps_mask is inherited; ps_pending is cleared
 *   - In POSIX, the child starts executing at the point after the fork()
 *     call in the parent.  This requires thread cloning (Phase 3+); in
 *     Phase 2 the child struct proc is created but its Mach task has no
 *     threads.
 */
int bsd_fork(struct proc *parent, struct proc **child_out);

/*
 * bsd_exec — replace the process image with a new ELF executable.
 *
 * Loads the ELF binary at path into the process's address space, sets up a
 * new stack with argc/argv, and transfers control to the ELF entry point.
 *
 * proc:   the process performing exec()
 * path:   path to the ELF binary (resolved through the VFS server)
 * argc:   argument count
 * argv:   argument vector (NULL-terminated array of NUL-terminated strings)
 *
 * Returns 0 on success (at which point the old program image is gone and
 * the process is running the new binary), or a BSD errno on failure.
 *
 * On success exec() does NOT return to the caller in the traditional sense —
 * the calling thread's context is replaced.  In Phase 2 (no real user tasks),
 * bsd_exec() validates the ELF header and prepares the image stub, then
 * returns 0 to indicate "the exec would succeed" for testing purposes.
 *
 * Phase 2 limitations:
 *   - ELF loading requires the VFS server (Phase 3) for file access.
 *   - Address space replacement requires vm_map.c (Phase 2 partial).
 *   - We validate the ELF header of the in-memory image and set up the
 *     proc metadata, but do not map segments or transfer execution.
 */
int bsd_exec(struct proc *proc,
             const char *path,
             int argc,
             const char *const argv[]);

/*
 * bsd_exit — terminate a process with the given status.
 *
 * Performs:
 *   1. Close all file descriptors (fd_table_close_all).
 *   2. Destroy the Mach task (task_destroy).
 *   3. Transition proc state to PROC_STATE_ZOMBIE.
 *   4. Send SIGCHLD to the parent process.
 *   5. Re-parent any child processes to init (PID 1).
 *
 * proc:   the process calling exit()
 * status: raw exit status; encoded with BSD_W_EXITCODE(status, 0)
 *
 * This function does not return.  After it completes the calling process
 * is in ZOMBIE state and its Mach task has been destroyed.
 */
void bsd_exit(struct proc *proc, int status);

/*
 * bsd_wait — wait for a child process to change state.
 *
 * Searches for a ZOMBIE child of parent.  If found, the child is reaped
 * (struct proc freed) and the exit status is returned.
 *
 * parent:       the process calling wait()
 * target_pid:   BSD_WAIT_ANY to wait for any child; a specific PID otherwise
 * options:      BSD_WNOHANG, BSD_WUNTRACED (see bsd_proto.h)
 * status_out:   OUT — the encoded exit status (use BSD_W* macros to decode)
 *
 * Returns the PID of the reaped child on success, 0 if BSD_WNOHANG and
 * no child is immediately ready, or a negative BSD errno on error.
 */
int bsd_wait(struct proc *parent,
             pid_t target_pid,
             int options,
             int32_t *status_out);

/* =========================================================================
 * Credentials
 * ========================================================================= */

/*
 * bsd_getpid — return the PID of proc.
 */
pid_t bsd_getpid(const struct proc *proc);

/*
 * bsd_getppid — return the parent PID of proc.
 */
pid_t bsd_getppid(const struct proc *proc);

/* =========================================================================
 * IPC message dispatcher (Phase 3+)
 * ========================================================================= */

/*
 * bsd_dispatch — handle one incoming BSD RPC message.
 *
 * msg:      the raw Mach message received on the BSD server port
 * reply:    buffer for the reply message (caller allocates)
 *
 * Returns 0 if a reply was written to *reply, non-zero if the message
 * was unrecognised or malformed (caller should send an error reply).
 *
 * Phase 2: This function is demonstrated but not wired to a live receive
 * loop; it is called directly in bsd_server_main() with synthesised messages.
 */
int bsd_dispatch(const mach_msg_header_t *msg, mach_msg_header_t *reply);

#endif /* BSD_SERVER_H */
