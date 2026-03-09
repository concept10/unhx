/*
 * tests/bsd/bsd_server_test.c — BSD server test suite for NEOMACH
 *
 * Tests the Phase 2 BSD server: process table, fork, exec stub, exit, wait,
 * signal delivery, and file descriptor table operations.
 *
 * The test follows the same conventions as tests/ipc/ipc_roundtrip_test.c:
 *   [PASS] / [FAIL] lines on the serial console.
 *   Final summary line with pass/fail counts.
 *
 * Compiled into the kernel when NEOMACH_BOOT_TESTS=ON.
 * Called from kernel_main() after the existing IPC tests.
 */

#include "bsd_server_test.h"
#include "servers/bsd/bsd_server.h"
#include "servers/bsd/proc.h"
#include "servers/bsd/signal.h"
#include "servers/bsd/fd_table.h"
#include "kern/klib.h"

/* Serial output (from platform layer) */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* =========================================================================
 * Test infrastructure
 * ========================================================================= */

static int bsd_tests   = 0;
static int bsd_passed  = 0;
static int bsd_failed  = 0;

static void bsd_assert(const char *name, int condition)
{
    bsd_tests++;
    if (condition) {
        bsd_passed++;
        serial_putstr("  [PASS] ");
    } else {
        bsd_failed++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

/* =========================================================================
 * Test 1: Process table — alloc, find, free
 * ========================================================================= */

static void test_proc_table(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 1: process table\r\n");

    /* proc_find(BSD_PID_INIT) should always succeed after proc_init() */
    struct proc *init = proc_find(BSD_PID_INIT);
    bsd_assert("proc_find(PID 1) returns init", init != (void *)0);
    bsd_assert("init PID == 1", init && init->p_pid == BSD_PID_INIT);
    bsd_assert("init state is ACTIVE",
               init && init->p_state == PROC_STATE_ACTIVE);

    /* proc_find for a non-existent PID returns NULL */
    struct proc *nope = proc_find(BSD_PID_MAX - 1);
    bsd_assert("proc_find(unused PID) returns NULL", nope == (void *)0);

    /* Allocate a new process */
    struct proc *p = proc_alloc();
    bsd_assert("proc_alloc() succeeds", p != (void *)0);
    bsd_assert("new proc PID > 1", p && p->p_pid > BSD_PID_INIT);
    bsd_assert("new proc is ACTIVE",
               p && p->p_state == PROC_STATE_ACTIVE);

    if (p) {
        pid_t pid = p->p_pid;
        /* proc_find should now find it */
        bsd_assert("proc_find after alloc", proc_find(pid) == p);

        /* proc_free marks it HALTED */
        proc_free(p);
        bsd_assert("proc_find after free returns NULL",
                   proc_find(pid) == (void *)0);
    }
}

/* =========================================================================
 * Test 2: fd_table — init, alloc, close, dup, dup2
 * ========================================================================= */

static void test_fd_table(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 2: file descriptor table\r\n");

    struct fd_table fdt;
    fd_table_init(&fdt);

    /* Standard streams pre-allocated */
    bsd_assert("stdin (fd 0) is open",  fd_get(&fdt, STDIN_FILENO)  != (void *)0);
    bsd_assert("stdout (fd 1) is open", fd_get(&fdt, STDOUT_FILENO) != (void *)0);
    bsd_assert("stderr (fd 2) is open", fd_get(&fdt, STDERR_FILENO) != (void *)0);

    /* Allocate new fd — should get 3 (lowest available after 0,1,2) */
    int fd3 = fd_alloc(&fdt, O_RDONLY, MACH_PORT_NULL);
    bsd_assert("first alloc gets fd 3", fd3 == 3);

    int fd4 = fd_alloc(&fdt, O_WRONLY, MACH_PORT_NULL);
    bsd_assert("second alloc gets fd 4", fd4 == 4);

    /* Close fd 3, re-alloc should get fd 3 again (lowest-available) */
    int rc = fd_close(&fdt, fd3);
    bsd_assert("fd_close returns success", rc == BSD_ESUCCESS);

    int fd3b = fd_alloc(&fdt, O_RDWR, MACH_PORT_NULL);
    bsd_assert("alloc after close reuses fd 3", fd3b == 3);

    /* fd_dup */
    int fd5 = fd_dup(&fdt, fd4);
    bsd_assert("fd_dup(4) returns a new fd >= 0", fd5 >= 0 && fd5 != fd4);
    struct fd_entry *e4 = fd_get(&fdt, fd4);
    struct fd_entry *e5 = fd_get(&fdt, fd5);
    bsd_assert("dup shares vfs_port", e4 && e5 &&
               e4->fe_vfs_port == e5->fe_vfs_port);
    bsd_assert("dup clears O_CLOEXEC on new fd",
               e5 && !(e5->fe_flags & O_CLOEXEC));

    /* fd_dup2 */
    int rc2 = fd_dup2(&fdt, STDOUT_FILENO, 10);
    bsd_assert("fd_dup2(1, 10) succeeds", rc2 == 10);
    struct fd_entry *e10 = fd_get(&fdt, 10);
    bsd_assert("fd 10 now open after dup2", e10 != (void *)0);

    /* fd_close out-of-range */
    int rcbad = fd_close(&fdt, -1);
    bsd_assert("fd_close(-1) returns EBADF", rcbad == BSD_EBADF);

    int rcbad2 = fd_close(&fdt, FD_MAX);
    bsd_assert("fd_close(FD_MAX) returns EBADF", rcbad2 == BSD_EBADF);

    /* fd_table_close_all */
    fd_table_close_all(&fdt);
    bsd_assert("stdin closed after close_all",  fd_get(&fdt, STDIN_FILENO)  == (void *)0);
    bsd_assert("stdout closed after close_all", fd_get(&fdt, STDOUT_FILENO) == (void *)0);
    bsd_assert("fd 10 closed after close_all",  fd_get(&fdt, 10) == (void *)0);

    /* O_CLOEXEC test with fd_table_cloexec */
    fd_table_init(&fdt);
    int clofd = fd_alloc(&fdt, O_RDONLY | O_CLOEXEC, MACH_PORT_NULL);
    int normfd = fd_alloc(&fdt, O_RDONLY, MACH_PORT_NULL);
    bsd_assert("cloexec fd allocated", clofd >= 0);
    bsd_assert("normal fd allocated",  normfd >= 0);

    fd_table_cloexec(&fdt);
    bsd_assert("O_CLOEXEC fd closed after exec",   fd_get(&fdt, clofd)  == (void *)0);
    bsd_assert("non-CLOEXEC fd open after exec",   fd_get(&fdt, normfd) != (void *)0);
}

/* =========================================================================
 * Test 3: signal — init, send, mask, handle
 * ========================================================================= */

static void test_signals(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 3: signal subsystem\r\n");

    /* Allocate a proc just for signal tests */
    struct proc *p = proc_alloc();
    bsd_assert("proc allocated for signal test", p != (void *)0);
    if (!p) return;

    /* Default state: no pending, no mask */
    bsd_assert("no pending signals initially",
               p->p_signals.ps_pending == 0);
    bsd_assert("empty signal mask initially",
               p->p_signals.ps_mask == 0);

    /* proc_signal enqueues a signal */
    proc_signal(p, SIGUSR1);
    bsd_assert("SIGUSR1 is pending", sigismember(p->p_signals.ps_pending, SIGUSR1));

    /* Sending the same signal twice: still one pending (not queued) */
    proc_signal(p, SIGUSR1);
    bsd_assert("duplicate signal: still one pending bit",
               sigismember(p->p_signals.ps_pending, SIGUSR1));

    /* Clear the pending signal by masking and re-checking */
    sigset_t mask;
    sigemptyset(mask);
    sigaddset(mask, SIGUSR1);
    proc_sigprocmask(p, BSD_SIG_BLOCK, &mask, (void *)0);
    bsd_assert("SIGUSR1 is now blocked", sigismember(p->p_signals.ps_mask, SIGUSR1));

    /* proc_handle_signals: SIGUSR1 is blocked so nothing happens */
    int killed = proc_handle_signals(p);
    bsd_assert("handle_signals with masked SIGUSR1 does not terminate proc", !killed);
    bsd_assert("SIGUSR1 still pending after masked delivery",
               sigismember(p->p_signals.ps_pending, SIGUSR1));

    /* Unblock and set action to SIG_IGN */
    proc_sigprocmask(p, BSD_SIG_UNBLOCK, &mask, (void *)0);
    struct sigaction sa;
    sa.sa_type         = BSD_SIG_ACTION_IGN;
    sa.sa_handler_addr = 0;
    sigemptyset(sa.sa_mask);
    sa.sa_flags        = 0;
    int rc = proc_sigaction(p, SIGUSR1, &sa, (void *)0);
    bsd_assert("proc_sigaction(SIG_IGN) succeeds", rc == BSD_ESUCCESS);

    /* Deliver: ignored, not terminated */
    killed = proc_handle_signals(p);
    bsd_assert("ignored SIGUSR1 does not terminate proc", !killed);
    bsd_assert("SIGUSR1 cleared after ignored delivery",
               !sigismember(p->p_signals.ps_pending, SIGUSR1));

    /* Test sigaction get/set round-trip */
    struct sigaction old_sa;
    struct sigaction new_sa;
    new_sa.sa_type         = BSD_SIG_ACTION_DFL;
    new_sa.sa_handler_addr = 0;
    sigemptyset(new_sa.sa_mask);
    new_sa.sa_flags        = 0;

    rc = proc_sigaction(p, SIGUSR2, &new_sa, &old_sa);
    bsd_assert("proc_sigaction round-trip succeeds", rc == BSD_ESUCCESS);
    bsd_assert("old action was DFL", old_sa.sa_type == BSD_SIG_ACTION_DFL);

    /* SIGKILL/SIGSTOP cannot have their disposition changed */
    rc = proc_sigaction(p, SIGKILL, &new_sa, (void *)0);
    bsd_assert("cannot change SIGKILL disposition (EINVAL)", rc == BSD_EINVAL);
    rc = proc_sigaction(p, SIGSTOP, &new_sa, (void *)0);
    bsd_assert("cannot change SIGSTOP disposition (EINVAL)", rc == BSD_EINVAL);

    /* default_action lookup */
    bsd_assert("SIGKILL default is TERM",
               sig_default_action(SIGKILL) == SIG_DEFAULT_TERM);
    bsd_assert("SIGCHLD default is IGN",
               sig_default_action(SIGCHLD) == SIG_DEFAULT_IGN);
    bsd_assert("SIGSTOP default is STOP",
               sig_default_action(SIGSTOP) == SIG_DEFAULT_STOP);
    bsd_assert("SIGCONT default is CONT",
               sig_default_action(SIGCONT) == SIG_DEFAULT_CONT);
    bsd_assert("SIGSEGV default is CORE",
               sig_default_action(SIGSEGV) == SIG_DEFAULT_CORE);

    /* Free without triggering termination paths */
    p->p_signals.ps_pending = 0;  /* clear so proc_free doesn't signal */
    proc_free(p);
}

/* =========================================================================
 * Test 4: fork
 * ========================================================================= */

static void test_fork(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 4: fork()\r\n");

    struct proc *parent = proc_alloc();
    bsd_assert("parent proc allocated", parent != (void *)0);
    if (!parent) return;

    parent->p_uid  = 1000;
    parent->p_gid  = 1000;
    parent->p_euid = 1000;
    parent->p_egid = 1000;

    /* Set up a SIGUSR1 handler in parent */
    struct sigaction sa;
    sa.sa_type         = BSD_SIG_ACTION_IGN;
    sa.sa_handler_addr = 0;
    sigemptyset(sa.sa_mask);
    sa.sa_flags        = 0;
    proc_sigaction(parent, SIGUSR1, &sa, (void *)0);

    /* Open an fd in parent */
    int pfd = fd_alloc(&parent->p_fd_table, O_RDONLY, MACH_PORT_NULL);
    bsd_assert("parent fd allocated", pfd >= 0);

    struct proc *child = (void *)0;
    int err = bsd_fork(parent, &child);
    bsd_assert("bsd_fork() returns success", err == BSD_ESUCCESS);
    bsd_assert("child proc is non-NULL", child != (void *)0);

    if (child) {
        bsd_assert("child PID != parent PID", child->p_pid != parent->p_pid);
        bsd_assert("child ppid == parent pid", child->p_ppid == parent->p_pid);
        bsd_assert("child uid inherited", child->p_uid == parent->p_uid);

        /* Child inherits open file descriptors */
        bsd_assert("child inherits parent fd",
                   fd_get(&child->p_fd_table, pfd) != (void *)0);

        /* Child inherits signal dispositions */
        bsd_assert("child inherits SIGUSR1=IGN",
                   child->p_signals.ps_actions[SIGUSR1].sa_type == BSD_SIG_ACTION_IGN);

        /* Child has no pending signals */
        bsd_assert("child has no pending signals",
                   child->p_signals.ps_pending == 0);

        /* child has a Mach task */
        bsd_assert("child has Mach task", child->p_task != (void *)0);

        /* Clean up child without triggering signal delivery */
        if (child->p_task) {
            task_destroy(child->p_task);
            child->p_task = (void *)0;
        }
        proc_free(child);
    }

    proc_free(parent);
}

/* =========================================================================
 * Test 5: exit and wait
 * ========================================================================= */

static void test_exit_wait(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 5: exit() and wait()\r\n");

    struct proc *parent = proc_alloc();
    bsd_assert("parent allocated for exit/wait test", parent != (void *)0);
    if (!parent) return;

    /* fork a child */
    struct proc *child = (void *)0;
    int err = bsd_fork(parent, &child);
    bsd_assert("fork for exit/wait test", err == BSD_ESUCCESS && child != (void *)0);
    if (!child) {
        proc_free(parent);
        return;
    }

    pid_t child_pid = child->p_pid;

    /* Child exits with status 42 */
    bsd_exit(child, BSD_W_EXITCODE(42, 0));
    /* After bsd_exit, child is in ZOMBIE state */
    bsd_assert("child is zombie after exit",
               proc_find(child_pid) != (void *)0 &&
               proc_find(child_pid)->p_state == PROC_STATE_ZOMBIE);

    /* Parent should have received SIGCHLD */
    bsd_assert("parent received SIGCHLD",
               sigismember(parent->p_signals.ps_pending, SIGCHLD));

    /* Wait reaps the child */
    int32_t wstatus = 0;
    int reaped = bsd_wait(parent, BSD_WAIT_ANY, 0, &wstatus);
    bsd_assert("bsd_wait returns child pid", reaped == (int)child_pid);
    bsd_assert("wait exit status correct",
               BSD_WIFEXITED(wstatus) && BSD_WEXITSTATUS(wstatus) == 42);
    bsd_assert("child slot freed after wait",
               proc_find(child_pid) == (void *)0);

    /* WNOHANG with no zombie child returns 0 */
    int32_t dummy = 0;
    int rc = bsd_wait(parent, BSD_WAIT_ANY, BSD_WNOHANG, &dummy);
    bsd_assert("WNOHANG with no zombie child returns 0", rc == 0);

    proc_free(parent);
}

/* =========================================================================
 * Test 6: signal-driven termination via SIGTERM
 * ========================================================================= */

static void test_signal_termination(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 6: signal-driven termination\r\n");

    struct proc *parent = proc_alloc();
    bsd_assert("parent allocated for signal-term test", parent != (void *)0);
    if (!parent) return;

    struct proc *child = (void *)0;
    bsd_fork(parent, &child);
    bsd_assert("child forked", child != (void *)0);
    if (!child) {
        proc_free(parent);
        return;
    }

    pid_t child_pid = child->p_pid;

    /* Send SIGTERM to child */
    proc_signal(child, SIGTERM);
    bsd_assert("SIGTERM pending on child",
               sigismember(child->p_signals.ps_pending, SIGTERM));

    /* Deliver: default action for SIGTERM is TERM, which calls bsd_exit */
    int terminated = proc_handle_signals(child);
    bsd_assert("proc_handle_signals returns 1 (process terminated)", terminated == 1);
    bsd_assert("child is zombie after SIGTERM delivery",
               proc_find(child_pid) != (void *)0 &&
               proc_find(child_pid)->p_state == PROC_STATE_ZOMBIE);
    bsd_assert("child exit encoded signal is SIGTERM",
               BSD_WIFSIGNALED(proc_find(child_pid)->p_exit_status) &&
               BSD_WTERMSIG(proc_find(child_pid)->p_exit_status) == SIGTERM);

    /* Parent reaps */
    int32_t wstatus = 0;
    int reaped = bsd_wait(parent, child_pid, 0, &wstatus);
    bsd_assert("parent reaps signal-terminated child", reaped == (int)child_pid);
    bsd_assert("exit was due to signal", BSD_WIFSIGNALED(wstatus));
    bsd_assert("exit signal is SIGTERM", BSD_WTERMSIG(wstatus) == SIGTERM);

    proc_free(parent);
}

/* =========================================================================
 * Test 7: exec() stub
 * ========================================================================= */

static void test_exec_stub(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 7: exec() stub\r\n");

    struct proc *p = proc_alloc();
    bsd_assert("proc for exec test allocated", p != (void *)0);
    if (!p) return;

    /* Allocate an O_CLOEXEC fd */
    int clofd = fd_alloc(&p->p_fd_table, O_RDONLY | O_CLOEXEC, MACH_PORT_NULL);
    bsd_assert("O_CLOEXEC fd allocated before exec", clofd >= 0);

    /* Set a custom signal handler */
    struct sigaction sa;
    sa.sa_type         = BSD_SIG_ACTION_FUNC;
    sa.sa_handler_addr = 0xDEADBEEF;
    sigemptyset(sa.sa_mask);
    sa.sa_flags        = 0;
    proc_sigaction(p, SIGUSR1, &sa, (void *)0);

    const char *argv[] = { "/bin/sh", (void *)0 };
    int rc = bsd_exec(p, "/bin/sh", 1, argv);
    bsd_assert("exec() returns success (stub)", rc == BSD_ESUCCESS);

    /* O_CLOEXEC fd should be closed after exec */
    bsd_assert("O_CLOEXEC fd closed by exec", fd_get(&p->p_fd_table, clofd) == (void *)0);

    /* Custom signal handler should be reset to SIG_DFL */
    bsd_assert("SIGUSR1 handler reset to DFL after exec",
               p->p_signals.ps_actions[SIGUSR1].sa_type == BSD_SIG_ACTION_DFL);

    proc_free(p);
}

/* =========================================================================
 * Test 8: IPC dispatcher
 * ========================================================================= */

static void test_ipc_dispatch(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 8: IPC message dispatcher\r\n");

    struct proc *p = proc_alloc();
    bsd_assert("proc for dispatch test allocated", p != (void *)0);
    if (!p) return;

    /* getpid request */
    bsd_getpid_request_t req;
    bsd_getpid_reply_t   rep;
    kmemset(&req, 0, sizeof(req));
    kmemset(&rep, 0, sizeof(rep));

    req.req.hdr.msgh_id   = BSD_MSG_GETPID;
    req.req.hdr.msgh_size = sizeof(req);
    req.req.bsd_pid       = (int32_t)p->p_pid;

    int rc = bsd_dispatch((const mach_msg_header_t *)&req,
                          (mach_msg_header_t *)&rep);
    bsd_assert("bsd_dispatch(GETPID) returns 0", rc == 0);
    bsd_assert("GETPID reply has correct pid",
               rep.rep.bsd_retval == (int32_t)p->p_pid);
    bsd_assert("GETPID reply errno is 0", rep.rep.bsd_errno == BSD_ESUCCESS);

    /* getpid with unknown pid */
    req.req.bsd_pid = BSD_PID_MAX - 1;
    kmemset(&rep, 0, sizeof(rep));
    bsd_dispatch((const mach_msg_header_t *)&req, (mach_msg_header_t *)&rep);
    bsd_assert("GETPID with bad pid returns ESRCH",
               rep.rep.bsd_errno == BSD_ESRCH);

    /* Unknown message ID */
    bsd_reply_header_t unk_rep;
    kmemset(&req, 0, sizeof(req));
    kmemset(&unk_rep, 0, sizeof(unk_rep));
    req.req.hdr.msgh_id = 0xFFFF;
    bsd_dispatch((const mach_msg_header_t *)&req,
                 (mach_msg_header_t *)&unk_rep);
    bsd_assert("unknown msg ID returns EINVAL",
               unk_rep.bsd_errno == BSD_EINVAL);

    proc_free(p);
}

/* =========================================================================
 * Test 9: re-parenting orphans to init on exit
 * ========================================================================= */

static void test_reparent(void)
{
    serial_putstr("\r\n[BSD-TEST] Test 9: orphan re-parenting\r\n");

    struct proc *parent = proc_alloc();
    bsd_assert("parent allocated", parent != (void *)0);
    if (!parent) return;

    struct proc *child = (void *)0;
    bsd_fork(parent, &child);
    bsd_assert("child forked", child != (void *)0);
    if (!child) { proc_free(parent); return; }

    struct proc *grandchild = (void *)0;
    bsd_fork(child, &grandchild);
    bsd_assert("grandchild forked", grandchild != (void *)0);
    if (!grandchild) { proc_free(child); proc_free(parent); return; }

    pid_t gc_pid = grandchild->p_pid;

    /* child exits — grandchild should be re-parented to init */
    bsd_exit(child, BSD_W_EXITCODE(0, 0));

    bsd_assert("grandchild re-parented to init after parent exit",
               proc_find(gc_pid) != (void *)0 &&
               proc_find(gc_pid)->p_ppid == BSD_PID_INIT);

    /* Clean up grandchild */
    if (proc_find(gc_pid)) {
        if (proc_find(gc_pid)->p_task) {
            task_destroy(proc_find(gc_pid)->p_task);
            proc_find(gc_pid)->p_task = (void *)0;
        }
        proc_free(proc_find(gc_pid));
    }
    /* Reap child zombie */
    int32_t ws = 0;
    bsd_wait(parent, BSD_WAIT_ANY, 0, &ws);

    proc_free(parent);
}

/* =========================================================================
 * Top-level runner
 * ========================================================================= */

int bsd_server_test_run(void)
{
    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH BSD Server Test Suite (v0.4)\r\n");
    serial_putstr("========================================\r\n");

    bsd_tests  = 0;
    bsd_passed = 0;
    bsd_failed = 0;

    test_proc_table();
    test_fd_table();
    test_signals();
    test_fork();
    test_exit_wait();
    test_signal_termination();
    test_exec_stub();
    test_ipc_dispatch();
    test_reparent();

    serial_putstr("\r\n");
    serial_putstr("----------------------------------------\r\n");
    serial_putstr("  BSD Server Tests: ");
    serial_putdec((uint32_t)bsd_passed);
    serial_putstr(" passed, ");
    serial_putdec((uint32_t)bsd_failed);
    serial_putstr(" failed (");
    serial_putdec((uint32_t)bsd_tests);
    serial_putstr(" total)\r\n");
    serial_putstr("----------------------------------------\r\n");

    return (bsd_failed == 0) ? 0 : 1;
}
