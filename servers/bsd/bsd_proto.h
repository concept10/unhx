/*
 * servers/bsd/bsd_proto.h — BSD server IPC protocol for NEOMACH
 *
 * Defines the hand-written IPC message identifiers and message structures
 * used between userspace clients and the BSD personality server.
 *
 * DESIGN RATIONALE:
 *
 * Rather than using MIG (Mach Interface Generator), we define the protocol
 * by hand for Phase 2.  This gives us full visibility into the message
 * layout and avoids a MIG toolchain dependency while the build system is
 * still being established.  The message IDs and structures deliberately
 * mirror the naming conventions that MIG would generate, so a future
 * migration to MIG-generated stubs requires only mechanical translation.
 *
 * PROTOCOL OVERVIEW:
 *
 *   Every BSD syscall is modelled as a synchronous Mach RPC:
 *
 *     Client                         BSD Server
 *       |  BSD_MSG_* request          |
 *       |─────────────────────────────►|
 *       |                             | (processes request)
 *       |  BSD_RPL_* reply            |
 *       |◄─────────────────────────────|
 *
 *   The client sends to the well-known BSD server port (obtained via
 *   bootstrap_lookup("com.neomach.bsd")).  The reply port
 *   (msgh_local_port) is a per-call SEND_ONCE right created by the
 *   client so the server can route the reply back.
 *
 * MESSAGE ID RANGES:
 *
 *   0x0100–0x01FF  Process lifecycle  (fork, exec, exit, wait, getpid)
 *   0x0200–0x02FF  Signal operations  (sigaction, kill, sigprocmask)
 *   0x0300–0x03FF  File descriptors   (open, close, read, write, dup)
 *   0x0400–0x04FF  Credentials        (getuid, getgid, setuid, setgid)
 *
 * Reference: Mach 3.0 Server Writer's Guide (OSF/RI, 1992) §2 — MIG;
 *            GNU HURD hurd/hurd_types.h for analogous protocol definitions.
 */

#ifndef BSD_PROTO_H
#define BSD_PROTO_H

#include "mach/mach_types.h"
#include <stdint.h>

/* =========================================================================
 * Process lifecycle message IDs
 * ========================================================================= */

#define BSD_MSG_FORK        0x0101  /* fork()                               */
#define BSD_MSG_EXEC        0x0102  /* exec()                               */
#define BSD_MSG_EXIT        0x0103  /* exit()                               */
#define BSD_MSG_WAIT        0x0104  /* wait() / waitpid()                   */
#define BSD_MSG_GETPID      0x0105  /* getpid()                             */
#define BSD_MSG_GETPPID     0x0106  /* getppid()                            */

/* Reply IDs (request + 0x1000 convention matches MIG output) */
#define BSD_RPL_FORK        (BSD_MSG_FORK  | 0x1000)
#define BSD_RPL_EXEC        (BSD_MSG_EXEC  | 0x1000)
#define BSD_RPL_EXIT        (BSD_MSG_EXIT  | 0x1000)
#define BSD_RPL_WAIT        (BSD_MSG_WAIT  | 0x1000)
#define BSD_RPL_GETPID      (BSD_MSG_GETPID | 0x1000)
#define BSD_RPL_GETPPID     (BSD_MSG_GETPPID | 0x1000)

/* =========================================================================
 * Signal operation message IDs
 * ========================================================================= */

#define BSD_MSG_SIGACTION   0x0201  /* sigaction()                          */
#define BSD_MSG_KILL        0x0202  /* kill() — send signal to process      */
#define BSD_MSG_SIGPROCMASK 0x0203  /* sigprocmask()                        */
#define BSD_MSG_SIGPENDING  0x0204  /* sigpending()                         */

#define BSD_RPL_SIGACTION   (BSD_MSG_SIGACTION  | 0x1000)
#define BSD_RPL_KILL        (BSD_MSG_KILL        | 0x1000)
#define BSD_RPL_SIGPROCMASK (BSD_MSG_SIGPROCMASK | 0x1000)
#define BSD_RPL_SIGPENDING  (BSD_MSG_SIGPENDING  | 0x1000)

/* =========================================================================
 * File descriptor message IDs (backed by VFS server, Phase 3)
 * ========================================================================= */

#define BSD_MSG_OPEN        0x0301  /* open()                               */
#define BSD_MSG_CLOSE       0x0302  /* close()                              */
#define BSD_MSG_READ        0x0303  /* read()                               */
#define BSD_MSG_WRITE       0x0304  /* write()                              */
#define BSD_MSG_DUP         0x0305  /* dup()                                */
#define BSD_MSG_DUP2        0x0306  /* dup2()                               */
#define BSD_MSG_PIPE        0x0307  /* pipe()                               */

#define BSD_RPL_OPEN        (BSD_MSG_OPEN  | 0x1000)
#define BSD_RPL_CLOSE       (BSD_MSG_CLOSE | 0x1000)
#define BSD_RPL_READ        (BSD_MSG_READ  | 0x1000)
#define BSD_RPL_WRITE       (BSD_MSG_WRITE | 0x1000)
#define BSD_RPL_DUP         (BSD_MSG_DUP   | 0x1000)
#define BSD_RPL_DUP2        (BSD_MSG_DUP2  | 0x1000)
#define BSD_RPL_PIPE        (BSD_MSG_PIPE  | 0x1000)

/* =========================================================================
 * BSD return codes
 *
 * These are POSIX errno values encoded as 32-bit integers.  The BSD server
 * returns these in the bsd_reply_header_t.bsd_errno field.
 * ========================================================================= */

#define BSD_ESUCCESS    0
#define BSD_EPERM       1   /* Operation not permitted                       */
#define BSD_ENOENT      2   /* No such file or directory                     */
#define BSD_ESRCH       3   /* No such process                               */
#define BSD_EINTR       4   /* Interrupted system call                       */
#define BSD_EIO         5   /* Input/output error                            */
#define BSD_ENOMEM     12   /* Out of memory                                 */
#define BSD_EBADF       9   /* Bad file descriptor                           */
#define BSD_EACCES     13   /* Permission denied                             */
#define BSD_EFAULT     14   /* Bad address                                   */
#define BSD_EBUSY      16   /* Device or resource busy                       */
#define BSD_EEXIST     17   /* File exists                                   */
#define BSD_EINVAL     22   /* Invalid argument                              */
#define BSD_EMFILE     24   /* Too many open files                           */
#define BSD_ECHILD     10   /* No child processes                            */
#define BSD_EAGAIN     11   /* Try again / resource temporarily unavailable  */

/* =========================================================================
 * Common message envelope
 *
 * Every BSD RPC request begins with a mach_msg_header_t followed by this
 * structure.  The pid field identifies the calling process (the kernel-side
 * BSD server verifies this against the task port in the message header).
 * ========================================================================= */

typedef struct {
    mach_msg_header_t   hdr;        /* standard Mach header                 */
    int32_t             bsd_pid;    /* caller PID                           */
} bsd_request_header_t;

/* Every BSD RPC reply begins with a mach_msg_header_t followed by this: */
typedef struct {
    mach_msg_header_t   hdr;        /* standard Mach header                 */
    int32_t             bsd_retval; /* syscall return value (−1 on error)   */
    int32_t             bsd_errno;  /* BSD error code (0 on success)        */
} bsd_reply_header_t;

/* =========================================================================
 * fork() — process fork
 * ========================================================================= */

/* Request: just the common header; no extra fields needed */
typedef struct {
    bsd_request_header_t    req;
} bsd_fork_request_t;

/* Reply: child PID (>0 in parent, 0 in child) */
typedef struct {
    bsd_reply_header_t  rep;
    int32_t             child_pid;  /* PID of newly created child           */
} bsd_fork_reply_t;

/* =========================================================================
 * exec() — execute a new program image
 * ========================================================================= */

#define BSD_PATH_MAX    256
#define BSD_ARGS_MAX    16
#define BSD_ARG_MAX     128

typedef struct {
    bsd_request_header_t    req;
    char                    path[BSD_PATH_MAX];         /* executable path  */
    uint32_t                argc;
    char                    argv[BSD_ARGS_MAX][BSD_ARG_MAX]; /* argument list */
} bsd_exec_request_t;

typedef struct {
    bsd_reply_header_t  rep;        /* only populated on error              */
} bsd_exec_reply_t;

/* =========================================================================
 * exit() — process termination
 * ========================================================================= */

typedef struct {
    bsd_request_header_t    req;
    int32_t                 exit_status;
} bsd_exit_request_t;

/* exit() has no reply (the process terminates) */

/* =========================================================================
 * wait() / waitpid()
 * ========================================================================= */

#define BSD_WAIT_ANY    (-1)    /* wait for any child (like waitpid(-1,...)) */
#define BSD_WNOHANG     (1 << 0) /* return immediately if no child is done  */
#define BSD_WUNTRACED   (1 << 1) /* also return for stopped children        */

typedef struct {
    bsd_request_header_t    req;
    int32_t                 pid;    /* target child PID, or BSD_WAIT_ANY    */
    int32_t                 options;
} bsd_wait_request_t;

typedef struct {
    bsd_reply_header_t  rep;
    int32_t             child_pid;      /* PID of reaped child              */
    int32_t             exit_status;    /* encoded exit status              */
} bsd_wait_reply_t;

/* Status encoding helpers (mirrors sys/wait.h macros) */
#define BSD_WIFEXITED(s)    (((s) & 0x7f) == 0)
#define BSD_WEXITSTATUS(s)  (((s) >> 8) & 0xff)
#define BSD_WIFSIGNALED(s)  (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define BSD_WTERMSIG(s)     ((s) & 0x7f)
#define BSD_W_EXITCODE(ret, sig) (((ret) << 8) | ((sig) & 0x7f))

/* =========================================================================
 * getpid() / getppid()
 * ========================================================================= */

typedef struct {
    bsd_request_header_t    req;
} bsd_getpid_request_t;

typedef struct {
    bsd_reply_header_t  rep;
    int32_t             pid;
} bsd_getpid_reply_t;

/* =========================================================================
 * sigaction() — examine and change a signal action
 * ========================================================================= */

/* Encoded signal handler representation (fits in a 32-bit field) */
#define BSD_SIG_DFL     0   /* default action                               */
#define BSD_SIG_IGN     1   /* ignore signal                                */
#define BSD_SIG_CUSTOM  2   /* user-supplied handler (address in user space) */

typedef struct {
    uint32_t    sa_handler_type;    /* BSD_SIG_DFL / _IGN / _CUSTOM         */
    uint64_t    sa_handler_addr;    /* user-space handler address (CUSTOM)  */
    uint64_t    sa_mask;            /* signals to block during handler      */
    uint32_t    sa_flags;
} bsd_sigaction_t;

typedef struct {
    bsd_request_header_t    req;
    int32_t                 signum;
    int32_t                 has_new;    /* 1 if setting new action          */
    int32_t                 want_old;   /* 1 if requesting old action       */
    bsd_sigaction_t         new_action;
} bsd_sigaction_request_t;

typedef struct {
    bsd_reply_header_t  rep;
    bsd_sigaction_t     old_action;
} bsd_sigaction_reply_t;

/* =========================================================================
 * kill() — send a signal to a process
 * ========================================================================= */

typedef struct {
    bsd_request_header_t    req;
    int32_t                 target_pid; /* PID, 0 = process group, -1 = all */
    int32_t                 signum;
} bsd_kill_request_t;

typedef struct {
    bsd_reply_header_t  rep;
} bsd_kill_reply_t;

#endif /* BSD_PROTO_H */
