/*
 * servers/bsd/signal.c — POSIX signal subsystem for NEOMACH BSD server
 *
 * Implements signal state management, signal delivery, and sigaction.
 * See signal.h for the design rationale.
 */

#include "signal.h"
#include "proc.h"
#include "bsd_proto.h"
#include "kern/klib.h"

/* Forward declaration (bsd_exit is defined in bsd_server.c) */
extern void bsd_exit(struct proc *p, int status);

/* Serial output for debug/trace */
extern void serial_putstr(const char *s);
extern void serial_putdec(uint32_t val);

/* =========================================================================
 * Default signal action table
 * ========================================================================= */

/*
 * One entry per signal (index 1 .. NSIG-1; index 0 unused).
 */
static const sig_default_action_t default_actions[NSIG] = {
    /* 0 */  SIG_DEFAULT_IGN,    /* unused — signal 0 is not a real signal  */
    /* 1  SIGHUP  */  SIG_DEFAULT_TERM,
    /* 2  SIGINT  */  SIG_DEFAULT_TERM,
    /* 3  SIGQUIT */  SIG_DEFAULT_CORE,
    /* 4  SIGILL  */  SIG_DEFAULT_CORE,
    /* 5  SIGTRAP */  SIG_DEFAULT_CORE,
    /* 6  SIGABRT */  SIG_DEFAULT_CORE,
    /* 7  SIGBUS  */  SIG_DEFAULT_CORE,
    /* 8  SIGFPE  */  SIG_DEFAULT_CORE,
    /* 9  SIGKILL */  SIG_DEFAULT_TERM,   /* cannot be caught or ignored    */
    /* 10 SIGUSR1 */  SIG_DEFAULT_TERM,
    /* 11 SIGSEGV */  SIG_DEFAULT_CORE,
    /* 12 SIGUSR2 */  SIG_DEFAULT_TERM,
    /* 13 SIGPIPE */  SIG_DEFAULT_TERM,
    /* 14 SIGALRM */  SIG_DEFAULT_TERM,
    /* 15 SIGTERM */  SIG_DEFAULT_TERM,
    /* 16 SIGSTKFLT */ SIG_DEFAULT_TERM,
    /* 17 SIGCHLD */  SIG_DEFAULT_IGN,    /* default: ignore                */
    /* 18 SIGCONT */  SIG_DEFAULT_CONT,
    /* 19 SIGSTOP */  SIG_DEFAULT_STOP,   /* cannot be caught or ignored    */
    /* 20 SIGTSTP */  SIG_DEFAULT_STOP,
    /* 21 SIGTTIN */  SIG_DEFAULT_STOP,
    /* 22 SIGTTOU */  SIG_DEFAULT_STOP,
    /* 23 SIGURG  */  SIG_DEFAULT_IGN,
    /* 24 SIGXCPU */  SIG_DEFAULT_CORE,
    /* 25 SIGXFSZ */  SIG_DEFAULT_CORE,
    /* 26 SIGVTALRM */ SIG_DEFAULT_TERM,
    /* 27 SIGPROF */  SIG_DEFAULT_TERM,
    /* 28 SIGWINCH */ SIG_DEFAULT_IGN,
    /* 29 SIGIO   */  SIG_DEFAULT_TERM,
    /* 30 SIGPWR  */  SIG_DEFAULT_TERM,
    /* 31 SIGSYS  */  SIG_DEFAULT_CORE,
};

sig_default_action_t sig_default_action(int sig)
{
    if (sig < 1 || sig >= NSIG)
        return SIG_DEFAULT_TERM;
    return default_actions[sig];
}

/* =========================================================================
 * proc_signals_init
 * ========================================================================= */

void proc_signals_init(struct proc_signals *ps)
{
    int i;
    sigemptyset(ps->ps_pending);
    sigemptyset(ps->ps_mask);
    for (i = 0; i < NSIG; i++) {
        ps->ps_actions[i].sa_type         = BSD_SIG_ACTION_DFL;
        ps->ps_actions[i].sa_handler_addr = 0;
        sigemptyset(ps->ps_actions[i].sa_mask);
        ps->ps_actions[i].sa_flags        = 0;
    }
}

/* =========================================================================
 * proc_signal
 * ========================================================================= */

void proc_signal(struct proc *p, int sig)
{
    if (!p || sig < 1 || sig >= NSIG)
        return;

    if (p->p_state != PROC_STATE_ACTIVE)
        return;

    /*
     * SIGKILL and SIGSTOP bypass the signal mask and cannot be ignored.
     * All other signals that are currently blocked are still recorded as
     * pending — they will be delivered when the mask allows.
     */
    sigaddset(p->p_signals.ps_pending, sig);
}

/* =========================================================================
 * proc_handle_signals
 * ========================================================================= */

int proc_handle_signals(struct proc *p)
{
    int sig;

    if (!p || p->p_state != PROC_STATE_ACTIVE)
        return 0;

    for (sig = 1; sig < NSIG; sig++) {
        if (!sigismember(p->p_signals.ps_pending, sig))
            continue;

        /*
         * SIGKILL and SIGSTOP bypass the blocking mask.
         * For all other signals, skip if masked.
         */
        if (sig != SIGKILL && sig != SIGSTOP) {
            if (sigismember(p->p_signals.ps_mask, sig))
                continue;   /* signal is masked — keep it pending           */
        }

        /* Remove from pending set before dispatch to avoid re-entry */
        sigdelset(p->p_signals.ps_pending, sig);

        struct sigaction *sa = &p->p_signals.ps_actions[sig];

        if (sa->sa_type == BSD_SIG_ACTION_IGN) {
            /* Explicitly ignored — discard (but SIGKILL/SIGSTOP can't be) */
            if (sig == SIGKILL || sig == SIGSTOP)
                sa->sa_type = BSD_SIG_ACTION_DFL; /* force default */
            else
                continue;
        }

        if (sa->sa_type == BSD_SIG_ACTION_DFL) {
            sig_default_action_t def = sig_default_action(sig);

            switch (def) {
            case SIG_DEFAULT_IGN:
                /* e.g. SIGCHLD default: ignore */
                break;

            case SIG_DEFAULT_TERM:
            case SIG_DEFAULT_CORE:
                /*
                 * Terminate the process.
                 * bsd_exit() will not return; after it, the proc is in
                 * ZOMBIE state and we must not touch it again.
                 */
                bsd_exit(p, BSD_W_EXITCODE(0, sig));
                return 1;   /* process terminated                           */

            case SIG_DEFAULT_STOP:
                /*
                 * Stop the process.  In Phase 2 we have no real process
                 * suspension, so we treat this as termination to avoid
                 * a permanent hang.  TODO (Phase 3): implement SIGCONT.
                 */
                bsd_exit(p, BSD_W_EXITCODE(0, sig));
                return 1;

            case SIG_DEFAULT_CONT:
                /* Continue: no-op if already running */
                break;
            }
        } else if (sa->sa_type == BSD_SIG_ACTION_FUNC) {
            /*
             * Custom user-space signal handler.
             *
             * Phase 2 stub: invoking a user-space function requires a
             * signal trampoline and OOL memory support (Phase 3+).
             * We log the signal and treat it as a no-op for now.
             *
             * In a full implementation we would:
             *   1. Save the thread's current register state.
             *   2. Set up a signal frame on the user stack.
             *   3. Point the thread's PC at sa_handler_addr.
             *   4. When the handler returns (via sigreturn trampoline),
             *      restore the saved state.
             */
            serial_putstr("[BSD signal] custom handler for sig ");
            serial_putdec((uint32_t)sig);
            serial_putstr(" (stub: Phase 3 trampoline required)\r\n");
        }

        /*
         * If SA_RESETHAND: restore default action after first delivery.
         */
        if (sa->sa_flags & SA_RESETHAND) {
            sa->sa_type = BSD_SIG_ACTION_DFL;
            sa->sa_handler_addr = 0;
        }
    }

    return 0;
}

/* =========================================================================
 * proc_sigaction
 * ========================================================================= */

int proc_sigaction(struct proc *p, int sig,
                   const struct sigaction *new_action,
                   struct sigaction *old_action)
{
    if (!p || sig < 1 || sig >= NSIG)
        return BSD_EINVAL;

    /* SIGKILL and SIGSTOP cannot have their disposition changed */
    if (new_action && (sig == SIGKILL || sig == SIGSTOP))
        return BSD_EINVAL;

    if (old_action)
        kmemcpy(old_action, &p->p_signals.ps_actions[sig],
                sizeof(struct sigaction));

    if (new_action)
        kmemcpy(&p->p_signals.ps_actions[sig], new_action,
                sizeof(struct sigaction));

    return BSD_ESUCCESS;
}

/* =========================================================================
 * proc_sigprocmask
 * ========================================================================= */

int proc_sigprocmask(struct proc *p, int how,
                     const sigset_t *newmask,
                     sigset_t *oldmask)
{
    if (!p)
        return BSD_EINVAL;

    if (oldmask)
        *oldmask = p->p_signals.ps_mask;

    if (!newmask)
        return BSD_ESUCCESS;

    switch (how) {
    case BSD_SIG_BLOCK:
        p->p_signals.ps_mask |= *newmask;
        break;
    case BSD_SIG_UNBLOCK:
        p->p_signals.ps_mask &= ~(*newmask);
        break;
    case BSD_SIG_SETMASK:
        p->p_signals.ps_mask = *newmask;
        break;
    default:
        return BSD_EINVAL;
    }

    /* SIGKILL and SIGSTOP can never be blocked */
    sigdelset(p->p_signals.ps_mask, SIGKILL);
    sigdelset(p->p_signals.ps_mask, SIGSTOP);

    return BSD_ESUCCESS;
}
