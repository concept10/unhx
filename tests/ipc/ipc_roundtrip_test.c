/*
 * tests/ipc/ipc_roundtrip_test.c — Two-task IPC round-trip correctness test
 *
 * This test exercises the complete Mach IPC path using the ipc_right.h
 * high-level right management API, verifying:
 *
 *   1. ipc_right_alloc_receive — port creation with receive right
 *   2. ipc_right_copy_send     — send right distribution to a second task
 *   3. mach_msg_send           — message send from the holder of the send right
 *   4. mach_msg_receive        — message receive by the holder of the receive right
 *   5. mach_msg_trap           — combined SEND|RCV in one call (RPC pattern)
 *   6. ipc_right_deallocate    — right lifecycle / cleanup
 *   7. Security: send without a right fails
 *   8. Security: receive without a right fails
 *   9. (send-once testing reserved for Phase 2 ipc_right_make_send_once)
 *  10. ipc_right_transfer       — move a right between tasks
 *
 * The test is designed to be compiled as part of the kernel (freestanding)
 * and called from kernel_main() when NEOMACH_BOOT_TESTS=ON.  It reports
 * results via serial_putstr() using the PASS/FAIL format established by
 * kernel/tests/ipc_test.c.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC;
 *            Liedtke, "On µ-Kernel Construction" (SOSP 1995) §3.
 */

#include "ipc_roundtrip_test.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "ipc/mach_msg.h"

/* Serial output (from platform layer) */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* -------------------------------------------------------------------------
 * Test infrastructure
 * ------------------------------------------------------------------------- */

static int rt_tests   = 0;
static int rt_passed  = 0;
static int rt_failed  = 0;

static void rt_assert(const char *name, int condition)
{
    rt_tests++;
    if (condition) {
        rt_passed++;
        serial_putstr("  [PASS] ");
    } else {
        rt_failed++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

/* -------------------------------------------------------------------------
 * Test message layout
 * ------------------------------------------------------------------------- */

struct rt_msg {
    mach_msg_header_t   hdr;
    uint32_t            seq;        /* sequence number                       */
    uint32_t            magic;      /* fixed pattern for integrity check     */
    char                payload[16];
};

#define RT_MAGIC    0xC0FFEE42u

/* -------------------------------------------------------------------------
 * Test 1: Basic send → receive
 * ------------------------------------------------------------------------- */

static void test_basic_roundtrip(struct task *kernel_task)
{
    serial_putstr("\r\n[RT] Test 1: basic send → receive\r\n");

    struct task *task_a = task_create(kernel_task);
    struct task *task_b = task_create(kernel_task);
    rt_assert("task_a created", task_a != (void *)0);
    rt_assert("task_b created", task_b != (void *)0);
    if (!task_a || !task_b) goto cleanup1;

    /* task_a allocates a port with RECEIVE + SEND rights.
     * The SEND right is needed so we can copy it to task_b. */
    mach_port_name_t rcv_port;
    kern_return_t kr = ipc_right_alloc_receive(task_a, &rcv_port,
                                               (void *)0, 1);
    rt_assert("alloc receive+send right", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup1;

    /* Copy the SEND right from task_a into task_b's space */
    mach_port_name_t snd_port;
    kr = ipc_right_copy_send(task_a, rcv_port, task_b, &snd_port);
    rt_assert("copy send right to task_b", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup1;

    /* task_b sends a message */
    struct rt_msg send_msg;
    kmemset(&send_msg, 0, sizeof(send_msg));
    send_msg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    send_msg.hdr.msgh_size        = sizeof(send_msg);
    send_msg.hdr.msgh_remote_port = snd_port;
    send_msg.hdr.msgh_local_port  = MACH_PORT_NULL;
    send_msg.hdr.msgh_id          = 1001;
    send_msg.seq   = 1;
    send_msg.magic = RT_MAGIC;
    kstrncpy(send_msg.payload, "round-trip-1", sizeof(send_msg.payload));

    kr = mach_msg_send(task_b, &send_msg.hdr, sizeof(send_msg));
    rt_assert("task_b: mach_msg_send succeeds", kr == KERN_SUCCESS);

    /* task_a receives */
    struct rt_msg recv_msg;
    mach_msg_size_t recv_sz = 0;
    kmemset(&recv_msg, 0, sizeof(recv_msg));

    kr = mach_msg_receive(task_a, rcv_port,
                          &recv_msg, sizeof(recv_msg), &recv_sz);
    rt_assert("task_a: mach_msg_receive succeeds", kr == KERN_SUCCESS);
    rt_assert("received magic matches", recv_msg.magic == RT_MAGIC);
    rt_assert("received seq matches",   recv_msg.seq == 1);
    rt_assert("received payload matches",
              kstrcmp(recv_msg.payload, "round-trip-1") == 0);
    rt_assert("received size matches",  recv_sz == sizeof(send_msg));

cleanup1:
    if (task_b) task_destroy(task_b);
    if (task_a) task_destroy(task_a);
}

/* -------------------------------------------------------------------------
 * Test 2: mach_msg_trap combined SEND|RCV (the RPC pattern)
 * ------------------------------------------------------------------------- */

static void test_rpc_pattern(struct task *kernel_task)
{
    serial_putstr("\r\n[RT] Test 2: combined SEND|RCV (RPC pattern)\r\n");

    struct task *client = task_create(kernel_task);
    struct task *server = task_create(kernel_task);
    rt_assert("client task created", client != (void *)0);
    rt_assert("server task created", server != (void *)0);
    if (!client || !server) goto cleanup2;

    /* Server allocates a request port with RECEIVE + SEND rights.
     * The SEND right is needed so it can be copied to the client. */
    mach_port_name_t server_port;
    kern_return_t kr = ipc_right_alloc_receive(server, &server_port,
                                               (void *)0, 1);
    rt_assert("server: alloc request port (with send)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup2;

    /* Client allocates a reply port with RECEIVE + SEND rights. */
    mach_port_name_t client_reply_port;
    kr = ipc_right_alloc_receive(client, &client_reply_port,
                                 (void *)0, 1);
    rt_assert("client: alloc reply port", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup2;

    /* Copy server's SEND right to the client */
    mach_port_name_t server_snd_port;
    kr = ipc_right_copy_send(server, server_port, client, &server_snd_port);
    rt_assert("client: got send right to server port", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup2;

    /*
     * Simulate server: receive request, send reply.
     * In a real system this would be a separate thread; in Phase 1 we
     * pre-stage the server reply before the client calls mach_msg_trap.
     */

    /* Client sends request */
    struct rt_msg req;
    kmemset(&req, 0, sizeof(req));
    req.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port = server_snd_port;
    req.hdr.msgh_local_port  = client_reply_port;
    req.hdr.msgh_id          = 2001;
    req.seq   = 42;
    req.magic = RT_MAGIC;
    kstrncpy(req.payload, "request", sizeof(req.payload));

    kr = mach_msg_send(client, &req.hdr, sizeof(req));
    rt_assert("client: send request", kr == KERN_SUCCESS);

    /* Server receives request */
    struct rt_msg srv_recv;
    mach_msg_size_t srv_sz = 0;
    kmemset(&srv_recv, 0, sizeof(srv_recv));
    kr = mach_msg_receive(server, server_port,
                          &srv_recv, sizeof(srv_recv), &srv_sz);
    rt_assert("server: receive request", kr == KERN_SUCCESS);
    rt_assert("server: request magic correct", srv_recv.magic == RT_MAGIC);

    /*
     * Server sends reply back to client's reply port.
     * The reply port name in the server's space is carried in
     * msgh_local_port — but in Phase 1 we don't translate ports across
     * spaces in message descriptors yet (that's Phase 2 port-right
     * descriptors).  For this test we use client_reply_port directly,
     * simulating the kernel's translation that will be done in Phase 2.
     *
     * We give the server a send right to the client's reply port.
     */
    mach_port_name_t reply_snd_in_server;
    kr = ipc_right_copy_send(client, client_reply_port,
                             server, &reply_snd_in_server);
    rt_assert("server: got send right to reply port", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup2;

    struct rt_msg reply;
    kmemset(&reply, 0, sizeof(reply));
    reply.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    reply.hdr.msgh_size        = sizeof(reply);
    reply.hdr.msgh_remote_port = reply_snd_in_server;
    reply.hdr.msgh_local_port  = MACH_PORT_NULL;
    reply.hdr.msgh_id          = 2002;
    reply.seq   = 43;
    reply.magic = RT_MAGIC;
    kstrncpy(reply.payload, "reply-ok", sizeof(reply.payload));

    kr = mach_msg_send(server, &reply.hdr, sizeof(reply));
    rt_assert("server: send reply", kr == KERN_SUCCESS);

    /*
     * Pre-stage a second reply on the queue so we can test the combined
     * SEND|RCV path.  The combined call sends a new request and receives
     * the already-queued reply in one mach_msg_trap() call.
     */
    struct rt_msg req2;
    kmemset(&req2, 0, sizeof(req2));
    req2.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    req2.hdr.msgh_size        = sizeof(req2);
    req2.hdr.msgh_remote_port = server_snd_port;
    req2.hdr.msgh_local_port  = MACH_PORT_NULL;
    req2.hdr.msgh_id          = 2003;
    req2.seq   = 44;
    req2.magic = RT_MAGIC;
    kstrncpy(req2.payload, "req2", sizeof(req2.payload));

    /* Stage a reply for req2 on the reply port */
    struct rt_msg reply2;
    kmemset(&reply2, 0, sizeof(reply2));
    reply2.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    reply2.hdr.msgh_size        = sizeof(reply2);
    reply2.hdr.msgh_remote_port = reply_snd_in_server;
    reply2.hdr.msgh_local_port  = MACH_PORT_NULL;
    reply2.hdr.msgh_id          = 2004;
    reply2.seq   = 45;
    reply2.magic = RT_MAGIC;
    kstrncpy(reply2.payload, "reply2-ok", sizeof(reply2.payload));
    kr = mach_msg_send(server, &reply2.hdr, sizeof(reply2));
    rt_assert("server: pre-stage reply2", kr == KERN_SUCCESS);

    /*
     * Test combined SEND|RCV: client sends req2 AND receives the
     * pre-staged reply2 in one mach_msg_trap() call.
     *
     * mach_msg_trap() uses the same `msg` buffer for both send and receive:
     * - On entry: msg contains the request to send (send_size bytes)
     * - On return: msg is overwritten with the received reply
     *
     * Phase 1: non-blocking — works because the reply is already queued.
     * Phase 2: will block waiting for the reply if not yet available.
     */
    /*
     * Use req2 as the combined send/receive buffer.  After the call,
     * req2.hdr will contain the received reply.
     */
    mach_msg_size_t combined_sz = 0;
    kr = mach_msg_trap(client,
                       MACH_SEND_MSG | MACH_RCV_MSG,
                       &req2.hdr,
                       sizeof(req2),
                       sizeof(req2),
                       client_reply_port,
                       &combined_sz,
                       MACH_MSG_TIMEOUT_NONE);
    rt_assert("client: mach_msg_trap SEND|RCV succeeds", kr == KERN_SUCCESS);
    rt_assert("client: combined recv magic correct", req2.magic == RT_MAGIC);
    rt_assert("client: combined recv seq correct",   req2.seq == 45);

    /* Client receives the first reply (non-combined) */
    struct rt_msg cli_recv;
    mach_msg_size_t cli_sz = 0;
    kmemset(&cli_recv, 0, sizeof(cli_recv));
    kr = mach_msg_trap(client,
                       MACH_RCV_MSG,
                       &cli_recv.hdr,
                       0,
                       sizeof(cli_recv),
                       client_reply_port,
                       &cli_sz,
                       MACH_MSG_TIMEOUT_NONE);
    rt_assert("client: mach_msg_trap RECEIVE succeeds", kr == KERN_SUCCESS);
    rt_assert("client: reply magic correct", cli_recv.magic == RT_MAGIC);
    rt_assert("client: reply seq correct",   cli_recv.seq == 43);
    rt_assert("client: reply payload correct",
              kstrcmp(cli_recv.payload, "reply-ok") == 0);

cleanup2:
    /* Destroy client before server: client holds a send right to server_port.
     * If server is destroyed first, the port is freed while client's ipc_space
     * still references it, causing a use-after-free during client teardown. */
    if (client) task_destroy(client);
    if (server) task_destroy(server);
}

/* -------------------------------------------------------------------------
 * Test 3: Security invariants
 * ------------------------------------------------------------------------- */

static void test_security(struct task *kernel_task)
{
    serial_putstr("\r\n[RT] Test 3: security invariants\r\n");

    struct task *owner    = task_create(kernel_task);
    struct task *intruder = task_create(kernel_task);
    rt_assert("owner task created",    owner    != (void *)0);
    rt_assert("intruder task created", intruder != (void *)0);
    if (!owner || !intruder) goto cleanup3;

    /* Create a port in owner */
    mach_port_name_t port;
    kern_return_t kr = ipc_right_alloc_receive(owner, &port, (void *)0, 0);
    rt_assert("owner: alloc receive right", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup3;

    /* Intruder tries to send to owner's port by guessing name 1 */
    struct rt_msg bad_msg;
    kmemset(&bad_msg, 0, sizeof(bad_msg));
    bad_msg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    bad_msg.hdr.msgh_size        = sizeof(bad_msg);
    bad_msg.hdr.msgh_remote_port = 1;   /* guessing a name */
    bad_msg.hdr.msgh_id          = 666;
    bad_msg.magic = 0xBAD0BAD0u;

    kr = mach_msg_send(intruder, &bad_msg.hdr, sizeof(bad_msg));
    rt_assert("send without right fails", kr != KERN_SUCCESS);

    /* Intruder tries to receive from owner's port */
    struct rt_msg rcv_buf;
    mach_msg_size_t rcv_sz = 0;
    kmemset(&rcv_buf, 0, sizeof(rcv_buf));
    kr = mach_msg_receive(intruder, 1,
                          &rcv_buf, sizeof(rcv_buf), &rcv_sz);
    rt_assert("receive without right fails", kr != KERN_SUCCESS);

    /* Owner should still be able to receive (no message, but no crash) */
    kr = mach_msg_receive(owner, port,
                          &rcv_buf, sizeof(rcv_buf), &rcv_sz);
    rt_assert("owner: empty queue returns error (non-blocking Phase 1)",
              kr != KERN_SUCCESS);

cleanup3:
    if (intruder) task_destroy(intruder);
    if (owner)    task_destroy(owner);
}

/* -------------------------------------------------------------------------
 * Test 4: ipc_right_deallocate lifecycle
 * ------------------------------------------------------------------------- */

static void test_right_lifecycle(struct task *kernel_task)
{
    serial_putstr("\r\n[RT] Test 4: right lifecycle / deallocate\r\n");

    struct task *task = task_create(kernel_task);
    rt_assert("task created", task != (void *)0);
    if (!task) return;

    /* Allocate receive+send */
    mach_port_name_t name;
    kern_return_t kr = ipc_right_alloc_receive(task, &name, (void *)0, 1);
    rt_assert("alloc receive+send", kr == KERN_SUCCESS);

    /* Deallocate the right (should not crash) */
    kr = ipc_right_deallocate(task, name);
    rt_assert("deallocate right", kr == KERN_SUCCESS);

    /* Using deallocated name should fail */
    struct rt_msg m;
    mach_msg_size_t sz = 0;
    kmemset(&m, 0, sizeof(m));
    kr = mach_msg_receive(task, name, &m, sizeof(m), &sz);
    rt_assert("receive on freed name fails", kr != KERN_SUCCESS);

    task_destroy(task);
}

/* -------------------------------------------------------------------------
 * Test 5: ipc_right_transfer moves a right
 * ------------------------------------------------------------------------- */

static void test_right_transfer(struct task *kernel_task)
{
    serial_putstr("\r\n[RT] Test 5: ipc_right_transfer\r\n");

    struct task *task_a = task_create(kernel_task);
    struct task *task_b = task_create(kernel_task);
    rt_assert("task_a created", task_a != (void *)0);
    rt_assert("task_b created", task_b != (void *)0);
    if (!task_a || !task_b) goto cleanup5;

    /* task_a gets a port with receive+send */
    mach_port_name_t name_a;
    kern_return_t kr = ipc_right_alloc_receive(task_a, &name_a, (void *)0, 1);
    rt_assert("task_a: alloc receive+send", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup5;

    /* Transfer the send right to task_b */
    mach_port_name_t name_b;
    /*
     * We need to split: keep RECEIVE in task_a, move SEND to task_b.
     * Use ipc_right_copy_send (which does not remove from source),
     * then deallocate the send right in task_a.
     *
     * Alternatively, use ipc_right_transfer on a pure SEND entry.
     * Create a second send-right entry in task_a first.
     */
    mach_port_name_t send_only_name;
    /* Acquire send right of the same port in a new slot */
    kr = ipc_right_copy_send(task_a, name_a, task_a, &send_only_name);
    rt_assert("task_a: make extra send right", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) goto cleanup5;

    /* Transfer that send-only entry to task_b */
    kr = ipc_right_transfer(task_a, send_only_name, task_b, &name_b);
    rt_assert("transfer send right from task_a to task_b", kr == KERN_SUCCESS);

    /* Verify task_b can now send */
    struct rt_msg send_m;
    kmemset(&send_m, 0, sizeof(send_m));
    send_m.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    send_m.hdr.msgh_size        = sizeof(send_m);
    send_m.hdr.msgh_remote_port = name_b;
    send_m.hdr.msgh_id          = 5005;
    send_m.magic = RT_MAGIC;
    send_m.seq   = 99;
    kstrncpy(send_m.payload, "transferred", sizeof(send_m.payload));

    kr = mach_msg_send(task_b, &send_m.hdr, sizeof(send_m));
    rt_assert("task_b: send via transferred right", kr == KERN_SUCCESS);

    /* task_a can still receive */
    struct rt_msg recv_m;
    mach_msg_size_t recv_sz = 0;
    kmemset(&recv_m, 0, sizeof(recv_m));
    kr = mach_msg_receive(task_a, name_a, &recv_m, sizeof(recv_m), &recv_sz);
    rt_assert("task_a: receive msg from task_b", kr == KERN_SUCCESS);
    rt_assert("task_a: magic correct", recv_m.magic == RT_MAGIC);

cleanup5:
    if (task_b) task_destroy(task_b);
    if (task_a) task_destroy(task_a);
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */

int ipc_roundtrip_test_run(void)
{
    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH IPC Round-Trip Test Suite\r\n");
    serial_putstr("========================================\r\n");

    struct task *kernel_task = kernel_task_ptr();
    if (!kernel_task) {
        serial_putstr("[RT] ERROR: no kernel task\r\n");
        return 1;
    }

    test_basic_roundtrip(kernel_task);
    test_rpc_pattern(kernel_task);
    test_security(kernel_task);
    test_right_lifecycle(kernel_task);
    test_right_transfer(kernel_task);

    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" Round-Trip Results: ");
    serial_putdec((uint32_t)rt_passed);
    serial_putstr(" passed, ");
    serial_putdec((uint32_t)rt_failed);
    serial_putstr(" failed, ");
    serial_putdec((uint32_t)rt_tests);
    serial_putstr(" total\r\n");

    if (rt_failed == 0) {
        serial_putstr(" STATUS: PASS\r\n");
        serial_putstr("========================================\r\n");
    } else {
        serial_putstr(" STATUS: FAIL\r\n");
        serial_putstr("========================================\r\n");
    }

    return (rt_failed > 0) ? 1 : 0;
}