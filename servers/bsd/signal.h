/*
 * servers/bsd/signal.h — POSIX signal subsystem for NEOMACH BSD server
 *
 * Defines signal numbers, signal actions, and the per-process signal state
 * stored in struct proc.  Signal delivery is modelled after the POSIX.1-2017
 * specification, adapted for the NEOMACH multi-server architecture.
 *
 * SIGNAL DELIVERY MODEL:
 *
 * In a conventional kernel, signals are delivered by modifying the user-mode
 * register state before returning from a syscall or interrupt.  In a Mach
 * multi-server environment, there is no such "return path" for signals
 * crossing server boundaries.
 *
 * The NEOMACH approach (informed by GNU HURD experience) is:
 *
 *   1. The BSD server maintains per-process signal state (pending signals,
 *      masks, actions) in struct proc.
 *
 *   2. When a signal is sent to a process (via kill() or internally), the
 *      server sets a bit in p_pending_sigs and, if the thread is blocked
 *      on a Mach port receive, interrupts the blocking receive.
 *
 *   3. On the next server-side dispatch of a request from the target process
 *      (or when proc_handle_signals() is called explicitly), pending signals
 *      are processed in ascending signal-number order.
 *
 *   4. Default-action signals (SIGKILL, SIGTERM) cause bsd_exit() to be
 *      called immediately.  Ignored signals are discarded.  Custom-handler
 *      signals are delivered to the process via a special "signal delivery"
 *      Mach message that sets up a trampoline in the process's address space.
 *
 * Phase 2 NOTE:
 *   Signal handler invocation across task boundaries requires user-mode
 *   signal trampolines and OOL memory, both of which depend on VM features
 *   not yet implemented.  In Phase 2 we fully implement the kernel-side
 *   signal state (pending, masked, default/ignore) and deliver signals whose
 *   default action is process termination or ignorance.  The custom handler
 *   path is a well-documented stub.
 *
 * Reference: Stevens, "Advanced Programming in the UNIX Environment" §10;
 *            GNU HURD hurd/signal.h for multi-server signal design;
 *            POSIX.1-2017 §2.4 — Signal Concepts.
 */

#ifndef BSD_SIGNAL_H
#define BSD_SIGNAL_H

#include <stdint.h>

/* Forward declarations */
struct proc;

/* =========================================================================
 * Signal numbers (POSIX + BSD extensions)
 *
 * These match the historic BSD/Linux numbering so that binaries compiled
 * for Linux or macOS use the same signal constants without translation.
 * ========================================================================= */

#define NSIG        32      /* total number of signals (indices 1–31)       */

#define SIGHUP      1       /* Hangup                                        */
#define SIGINT      2       /* Interrupt (Ctrl-C)                            */
#define SIGQUIT     3       /* Quit (Ctrl-\)                                 */
#define SIGILL      4       /* Illegal instruction                           */
#define SIGTRAP     5       /* Trace/breakpoint trap                         */
#define SIGABRT     6       /* Abort (from abort())                          */
#define SIGBUS      7       /* Bus error (bad memory access)                 */
#define SIGFPE      8       /* Floating-point exception                      */
#define SIGKILL     9       /* Kill (cannot be caught or ignored)            */
#define SIGUSR1     10      /* User-defined signal 1                         */
#define SIGSEGV     11      /* Segmentation fault                            */
#define SIGUSR2     12      /* User-defined signal 2                         */
#define SIGPIPE     13      /* Broken pipe                                   */
#define SIGALRM     14      /* Timer signal from alarm()                     */
#define SIGTERM     15      /* Termination signal                            */
#define SIGSTKFLT   16      /* Stack fault (Linux; unused on NEOMACH)        */
#define SIGCHLD     17      /* Child stopped or terminated                   */
#define SIGCONT     18      /* Continue if stopped                           */
#define SIGSTOP     19      /* Stop process (cannot be caught or ignored)    */
#define SIGTSTP     20      /* Stop typed at terminal (Ctrl-Z)               */
#define SIGTTIN     21      /* Background read from terminal                 */
#define SIGTTOU     22      /* Background write to terminal                  */
#define SIGURG      23      /* Urgent data on socket                         */
#define SIGXCPU     24      /* CPU time limit exceeded                       */
#define SIGXFSZ     25      /* File size limit exceeded                      */
#define SIGVTALRM   26      /* Virtual timer signal                          */
#define SIGPROF     27      /* Profiling timer signal                        */
#define SIGWINCH    28      /* Window size change                            */
#define SIGIO       29      /* I/O now possible (also SIGPOLL)               */
#define SIGPWR      30      /* Power failure (Linux; BSD: SIGINFO)           */
#define SIGSYS      31      /* Bad system call argument                      */

/* =========================================================================
 * sigset_t — a bitmask of signal numbers
 *
 * Bit N (0-indexed) represents signal N+1.  We use uint64_t so that up to
 * 64 signals can be represented without changing the type.  POSIX only
 * requires 32 standard signals; the upper 32 bits are reserved for future
 * real-time signals (SIGRTMIN–SIGRTMAX).
 * ========================================================================= */

typedef uint64_t sigset_t;

/* Signal mask helpers */
#define SIGMASK(sig)            ((sigset_t)1 << ((sig) - 1))
#define sigaddset(set, sig)     ((set) |=  SIGMASK(sig))
#define sigdelset(set, sig)     ((set) &= ~SIGMASK(sig))
#define sigismember(set, sig)   (!!((set) & SIGMASK(sig)))
#define sigemptyset(set)        ((set) = (sigset_t)0)
#define sigfillset(set)         ((set) = ~(sigset_t)0)

/*
 * Signals that cannot be caught, blocked, or ignored.
 * POSIX.1-2017 §2.4.3: "SIGKILL and SIGSTOP cannot be caught or ignored."
 */
#define SIG_UNCATCHABLE_MASK    (SIGMASK(SIGKILL) | SIGMASK(SIGSTOP))

/* =========================================================================
 * Signal disposition (sigaction)
 * ========================================================================= */

/*
 * sa_handler type codes.  We use integer codes rather than function pointers
 * because the "function pointer" may point to user-mode code in a different
 * address space, which the kernel cannot call directly.
 */
typedef enum {
    BSD_SIG_ACTION_DFL  = 0,    /* default action                           */
    BSD_SIG_ACTION_IGN  = 1,    /* ignore                                   */
    BSD_SIG_ACTION_FUNC = 2,    /* custom user-space handler function       */
} bsd_sig_action_type_t;

/*
 * struct sigaction — the action to take for one signal.
 *
 * In a real POSIX system, sa_handler is a function pointer.  Here we keep
 * the handler address as a uint64_t (the user-space virtual address of the
 * handler function) because the BSD server and the process may be in
 * different address spaces.  Phase 2 delivers SIG_DFL and SIG_IGN actions;
 * SIG_FUNC requires the signal trampoline (Phase 3+).
 */
struct sigaction {
    bsd_sig_action_type_t   sa_type;        /* disposition type             */
    uint64_t                sa_handler_addr;/* user-space handler (FUNC)    */
    sigset_t                sa_mask;        /* signals blocked during handler*/
    uint32_t                sa_flags;       /* SA_RESTART, SA_SIGINFO, etc. */
};

/* sa_flags */
#define SA_NOCLDSTOP    (1u << 0)   /* don't generate SIGCHLD on stop       */
#define SA_NOCLDWAIT    (1u << 1)   /* don't create zombies on child exit   */
#define SA_SIGINFO      (1u << 2)   /* handler takes (sig, info, ctx) args  */
#define SA_ONSTACK      (1u << 3)   /* deliver on alternate signal stack    */
#define SA_RESTART      (1u << 4)   /* restart syscalls on signal           */
#define SA_NODEFER      (1u << 5)   /* don't mask signal during handler     */
#define SA_RESETHAND    (1u << 6)   /* reset to SIG_DFL after first delivery*/

/* =========================================================================
 * Default signal actions
 *
 * POSIX defines five default actions:
 *   Term   — terminate the process
 *   Ign    — ignore
 *   Core   — terminate + core dump (stubbed: we treat as Term in Phase 2)
 *   Stop   — stop the process
 *   Cont   — continue if stopped
 * ========================================================================= */

typedef enum {
    SIG_DEFAULT_TERM  = 0,  /* terminate                                     */
    SIG_DEFAULT_IGN   = 1,  /* ignore                                        */
    SIG_DEFAULT_CORE  = 2,  /* core dump (Phase 2: treated as TERM)          */
    SIG_DEFAULT_STOP  = 3,  /* stop process                                  */
    SIG_DEFAULT_CONT  = 4,  /* continue                                      */
} sig_default_action_t;

/*
 * sig_default_action — return the default disposition for signal sig.
 * Used when sa_type == BSD_SIG_ACTION_DFL.
 */
sig_default_action_t sig_default_action(int sig);

/* =========================================================================
 * Per-process signal state
 *
 * Embedded in struct proc (see proc.h).
 * ========================================================================= */

struct proc_signals {
    sigset_t            ps_pending;         /* pending (unblocked) signals  */
    sigset_t            ps_mask;            /* blocked signal mask          */
    struct sigaction    ps_actions[NSIG];   /* per-signal disposition       */
};

/*
 * proc_signals_init — initialise the signal state to POSIX defaults.
 * All signals start with default action, empty mask, no pending signals.
 */
void proc_signals_init(struct proc_signals *ps);

/* =========================================================================
 * Signal operations (operate on a struct proc)
 * ========================================================================= */

/*
 * proc_signal — queue a signal for delivery to process p.
 *
 * If the signal is SIGKILL or SIGSTOP (uncatchable), the mask is bypassed.
 * If sig is already pending, this is a no-op (signals are not queued —
 * POSIX: "multiple occurrences of a signal may be collapsed into a single
 * pending signal").
 *
 * Thread safety: caller must hold the proc lock (Phase 2: spinlock on proc).
 */
void proc_signal(struct proc *p, int sig);

/*
 * proc_handle_signals — process all pending unmasked signals for proc p.
 *
 * Called:
 *   - Before returning a reply to the process after each BSD RPC
 *   - Explicitly after fork(), exec(), etc.
 *   - On timer tick (Phase 3+, preemptive delivery)
 *
 * Returns 1 if the process was terminated (caller should not use p again),
 * 0 otherwise.
 */
int proc_handle_signals(struct proc *p);

/*
 * proc_sigaction — examine or set the action for signal sig.
 *
 * If new_action is non-NULL, the action is updated.
 * If old_action is non-NULL, the previous action is stored there.
 *
 * Returns 0 on success, BSD_EINVAL on invalid signal number.
 */
int proc_sigaction(struct proc *p, int sig,
                   const struct sigaction *new_action,
                   struct sigaction *old_action);

/*
 * proc_sigprocmask — change the signal blocking mask.
 *
 * how:   0 = SIG_BLOCK (add to mask)
 *        1 = SIG_UNBLOCK (remove from mask)
 *        2 = SIG_SETMASK (replace mask)
 *
 * Returns 0 on success, BSD_EINVAL on bad 'how'.
 */
#define BSD_SIG_BLOCK    0
#define BSD_SIG_UNBLOCK  1
#define BSD_SIG_SETMASK  2

int proc_sigprocmask(struct proc *p, int how,
                     const sigset_t *newmask,
                     sigset_t *oldmask);

#endif /* BSD_SIGNAL_H */
