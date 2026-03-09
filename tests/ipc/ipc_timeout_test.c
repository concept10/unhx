/*
 * tests/ipc/ipc_timeout_test.c — Blocking receive with timeout test
 *
 * Exercises the Phase 2 blocking receive with timeout feature.
 * mach_msg_receive_timeout() and mach_msg_trap() with MACH_RCV_TIMEOUT
 * are tested in both the "already-queued message" and "timeout expires"
 * scenarios.
 *
 * NOTE: In Phase 2 the timeout is implemented as a TSC-based busy-wait.
 * The timeout granularity is approximately 1 ms on a 3 GHz processor.
 * Tests that expect KERN_OPERATION_TIMED_OUT use a 1 ms timeout (the
 * minimum that gives deterministic results in QEMU).
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.4 — mach_msg();
 *            OSF MK ipc/mach_msg.c MACH_RCV_TIMEOUT handling.
 */

#include "ipc_timeout_test.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "ipc/mach_msg.h"
#include "mach/mach_types.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_putdec(uint32_t val);

/* -------------------------------------------------------------------------
 * Test framework helpers
 * ------------------------------------------------------------------------- */

static int to_test_count = 0;
static int to_pass_count = 0;
static int to_fail_count = 0;

static void to_assert(const char *name, int cond)
{
    to_test_count++;
    if (cond) {
        to_pass_count++;
        serial_putstr("  [PASS] ");
    } else {
        to_fail_count++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

/* -------------------------------------------------------------------------
 * Test message type
 * ------------------------------------------------------------------------- */
typedef struct {
    mach_msg_header_t hdr;
    uint32_t          magic;
} to_msg_t;

/* -------------------------------------------------------------------------
 * Helper: create a port with receive right in ta, send right in tb
 * ------------------------------------------------------------------------- */
static kern_return_t
mk_pair(struct task **ta, struct task **tb,
        mach_port_name_t *rcv, mach_port_name_t *snd)
{
    *ta = task_create(kernel_task_ptr());
    *tb = task_create(kernel_task_ptr());
    if (!*ta || !*tb) return KERN_RESOURCE_SHORTAGE;

    struct ipc_port *port;
    kern_return_t kr = ipc_right_alloc_receive(*ta, rcv, &port, 0);
    if (kr != KERN_SUCCESS) { task_destroy(*ta); task_destroy(*tb); return kr; }
    kr = ipc_right_copy_send(*ta, *rcv, *tb, snd);
    if (kr != KERN_SUCCESS) { task_destroy(*ta); task_destroy(*tb); return kr; }
    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Test 1: Non-blocking receive on empty port
 * ------------------------------------------------------------------------- */
static void test_nonblocking_empty(void)
{
    serial_putstr("\r\n[Timeout Test 1] Non-blocking receive on empty port\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv, snd;
    kern_return_t kr = mk_pair(&ta, &tb, &rcv, &snd);
    to_assert("setup port pair", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* Receive on empty queue — should fail immediately */
    to_msg_t buf;
    mach_msg_size_t rsz = 0;
    kmemset(&buf, 0, sizeof(buf));

    kr = mach_msg_receive(ta, rcv, &buf, sizeof(buf), &rsz);
    to_assert("non-blocking receive on empty port returns error",
              kr != KERN_SUCCESS);

    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 2: Blocking receive with timeout expires
 * ------------------------------------------------------------------------- */
static void test_timeout_expires(void)
{
    serial_putstr("\r\n[Timeout Test 2] Blocking receive — timeout expires\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv, snd;
    kern_return_t kr = mk_pair(&ta, &tb, &rcv, &snd);
    to_assert("setup port pair (timeout)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* Receive on empty queue with a 1 ms timeout */
    to_msg_t buf;
    mach_msg_size_t rsz = 0;
    kmemset(&buf, 0, sizeof(buf));

    kr = mach_msg_receive_timeout(ta, rcv, &buf, sizeof(buf), &rsz,
                                   1 /* timeout_ms */);
    to_assert("receive with 1 ms timeout returns KERN_OPERATION_TIMED_OUT",
              kr == KERN_OPERATION_TIMED_OUT);

    (void)snd;
    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 3: Blocking receive succeeds when message is already queued
 * ------------------------------------------------------------------------- */
static void test_timeout_succeeds_with_queued(void)
{
    serial_putstr("\r\n[Timeout Test 3] Blocking receive succeeds with queued msg\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv, snd;
    kern_return_t kr = mk_pair(&ta, &tb, &rcv, &snd);
    to_assert("setup port pair (pre-queued)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* Pre-queue a message */
    to_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = snd;
    smsg.hdr.msgh_id          = 42;
    smsg.magic                = 0xC0DE0001u;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    to_assert("pre-queue message", kr == KERN_SUCCESS);

    /* Now receive with a 100 ms timeout — should succeed immediately */
    to_msg_t buf;
    mach_msg_size_t rsz = 0;
    kmemset(&buf, 0, sizeof(buf));

    kr = mach_msg_receive_timeout(ta, rcv, &buf, sizeof(buf), &rsz,
                                   100 /* timeout_ms */);
    to_assert("receive with timeout succeeds (msg was queued)",
              kr == KERN_SUCCESS);
    to_assert("received magic correct", buf.magic == 0xC0DE0001u);
    to_assert("msgh_id correct", buf.hdr.msgh_id == 42);

    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 4: mach_msg_trap with MACH_RCV_TIMEOUT — timeout expires
 * ------------------------------------------------------------------------- */
static void test_trap_timeout_expires(void)
{
    serial_putstr("\r\n[Timeout Test 4] mach_msg_trap MACH_RCV_TIMEOUT expires\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv, snd;
    kern_return_t kr = mk_pair(&ta, &tb, &rcv, &snd);
    to_assert("setup port pair (trap timeout)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    to_msg_t buf;
    mach_msg_size_t rsz = 0;
    kmemset(&buf, 0, sizeof(buf));

    /* MACH_RCV_MSG | MACH_RCV_TIMEOUT with 1 ms timeout on empty port */
    kr = mach_msg_trap(ta,
                        MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                        &buf.hdr,
                        0,
                        sizeof(buf),
                        rcv,
                        &rsz,
                        1 /* timeout_ms */);
    to_assert("mach_msg_trap MACH_RCV_TIMEOUT returns non-success",
              kr != KERN_SUCCESS);

    (void)snd;
    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 5: mach_msg_trap with MACH_RCV_TIMEOUT — succeeds with queued message
 * ------------------------------------------------------------------------- */
static void test_trap_timeout_with_queued(void)
{
    serial_putstr("\r\n[Timeout Test 5] mach_msg_trap MACH_RCV_TIMEOUT with queued\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv, snd;
    kern_return_t kr = mk_pair(&ta, &tb, &rcv, &snd);
    to_assert("setup port pair (trap queued)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* Pre-queue a message */
    to_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = snd;
    smsg.hdr.msgh_id          = 77;
    smsg.magic                = 0xFEED0002u;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    to_assert("pre-queue message (trap test)", kr == KERN_SUCCESS);

    to_msg_t buf;
    mach_msg_size_t rsz = 0;
    kmemset(&buf, 0, sizeof(buf));

    kr = mach_msg_trap(ta,
                        MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                        &buf.hdr,
                        0,
                        sizeof(buf),
                        rcv,
                        &rsz,
                        100 /* timeout_ms */);
    to_assert("mach_msg_trap MACH_RCV_TIMEOUT succeeds with queued msg",
              kr == KERN_SUCCESS);
    to_assert("received magic correct (trap)", buf.magic == 0xFEED0002u);

    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * ipc_timeout_test_run — entry point
 * ------------------------------------------------------------------------- */

int ipc_timeout_test_run(void)
{
    to_test_count = 0;
    to_pass_count = 0;
    to_fail_count = 0;

    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH IPC Timeout Test (Phase 2)\r\n");
    serial_putstr("========================================\r\n");

    test_nonblocking_empty();
    test_timeout_expires();
    test_timeout_succeeds_with_queued();
    test_trap_timeout_expires();
    test_trap_timeout_with_queued();

    serial_putstr("\r\n========================================\r\n");
    serial_putstr(" Timeout Results: ");
    serial_putdec(to_pass_count);
    serial_putstr(" passed, ");
    serial_putdec(to_fail_count);
    serial_putstr(" failed, ");
    serial_putdec(to_test_count);
    serial_putstr(" total\r\n");

    if (to_fail_count == 0) {
        serial_putstr(" STATUS: PASS\r\n");
        serial_putstr("========================================\r\n");
    } else {
        serial_putstr(" STATUS: FAIL\r\n");
        serial_putstr("========================================\r\n");
    }

    return (to_fail_count > 0) ? 1 : 0;
}
