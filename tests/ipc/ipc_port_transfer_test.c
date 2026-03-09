/*
 * tests/ipc/ipc_port_transfer_test.c — Port right transfer in messages test
 *
 * Exercises the Phase 2 port right transfer in messages.  Complex messages
 * (MACH_MSGH_BITS_COMPLEX) carrying mach_msg_port_descriptor_t entries
 * allow port rights to be transferred between tasks as part of a message.
 *
 * This is the fundamental mechanism for capability delegation in Mach: a
 * server can hand a client a send right to another port by embedding a port
 * descriptor in a reply message.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.4 — Messages;
 *            OSF MK ipc/mach_msg.c, ipc_right.c for right manipulation.
 */

#include "ipc_port_transfer_test.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "mach/mach_types.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_putdec(uint32_t val);

/* -------------------------------------------------------------------------
 * Test framework helpers
 * ------------------------------------------------------------------------- */

static int pt_test_count = 0;
static int pt_pass_count = 0;
static int pt_fail_count = 0;

static void pt_assert(const char *name, int cond)
{
    pt_test_count++;
    if (cond) {
        pt_pass_count++;
        serial_putstr("  [PASS] ");
    } else {
        pt_fail_count++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

/* -------------------------------------------------------------------------
 * Message types
 * ------------------------------------------------------------------------- */

/*
 * port_msg_t — complex message carrying one port right descriptor.
 */
typedef struct {
    mach_msg_header_t            hdr;
    mach_msg_body_t              body;
    mach_msg_port_descriptor_t   port_desc;
    uint32_t                     payload;
} port_msg_t;

/*
 * port_msg2_t — complex message carrying two port right descriptors.
 */
typedef struct {
    mach_msg_header_t            hdr;
    mach_msg_body_t              body;
    mach_msg_port_descriptor_t   desc0;
    mach_msg_port_descriptor_t   desc1;
    uint32_t                     payload;
} port_msg2_t;

/*
 * simple_msg_t — plain (non-complex) message for verifying port usability.
 */
typedef struct {
    mach_msg_header_t hdr;
    uint32_t          value;
} simple_msg_t;

/* -------------------------------------------------------------------------
 * Helper: create a task pair with a one-way port (task_a owns receive right)
 * ------------------------------------------------------------------------- */
static kern_return_t
setup_pair(struct task **ta_out, struct task **tb_out,
           mach_port_name_t *rcv_out, mach_port_name_t *snd_out)
{
    struct task *ta = task_create(kernel_task_ptr());
    struct task *tb = task_create(kernel_task_ptr());
    if (!ta || !tb) return KERN_RESOURCE_SHORTAGE;

    mach_port_name_t rcv, snd;
    struct ipc_port  *port;
    kern_return_t kr = ipc_right_alloc_receive(ta, &rcv, &port, 0);
    if (kr != KERN_SUCCESS) { task_destroy(ta); task_destroy(tb); return kr; }

    kr = ipc_right_copy_send(ta, rcv, tb, &snd);
    if (kr != KERN_SUCCESS) { task_destroy(ta); task_destroy(tb); return kr; }

    *ta_out  = ta;  *tb_out  = tb;
    *rcv_out = rcv; *snd_out = snd;
    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Helper: send a simple message to a port and verify receipt
 * ------------------------------------------------------------------------- */
static int
verify_send_recv(struct task *sender, mach_port_name_t send_name,
                 struct task *receiver, mach_port_name_t recv_name,
                 uint32_t magic)
{
    simple_msg_t sm;
    kmemset(&sm, 0, sizeof(sm));
    sm.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    sm.hdr.msgh_size        = sizeof(sm);
    sm.hdr.msgh_remote_port = send_name;
    sm.hdr.msgh_id          = 999;
    sm.value = magic;

    if (mach_msg_send(sender, &sm.hdr, sizeof(sm)) != KERN_SUCCESS)
        return 0;

    simple_msg_t rm;
    mach_msg_size_t rsz = 0;
    kmemset(&rm, 0, sizeof(rm));
    if (mach_msg_receive(receiver, recv_name, &rm, sizeof(rm), &rsz)
            != KERN_SUCCESS)
        return 0;

    return (rm.value == magic);
}

/* -------------------------------------------------------------------------
 * Test 1: COPY_SEND — sender keeps the right; receiver gets a copy
 * ------------------------------------------------------------------------- */
static void test_copy_send(void)
{
    serial_putstr("\r\n[Port Transfer Test 1] COPY_SEND\r\n");

    /* Port P is owned (receive) by task_a; task_b holds a send right */
    struct task *ta, *tb;
    mach_port_name_t rcv_a, snd_b;
    kern_return_t kr = setup_pair(&ta, &tb, &rcv_a, &snd_b);
    pt_assert("setup port pair", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* task_c will receive the COPY_SEND right via a message */
    struct task *tc = task_create(kernel_task_ptr());
    pt_assert("task_c created", tc != (void *)0);
    if (!tc) { task_destroy(ta); task_destroy(tb); return; }

    /* Mailbox port for task_c (task_b sends to it) */
    mach_port_name_t rcv_c, snd_to_c;
    struct ipc_port  *portC;
    kr = ipc_right_alloc_receive(tc, &rcv_c, &portC, 0);
    pt_assert("alloc port C receive", kr == KERN_SUCCESS);
    kr = ipc_right_copy_send(tc, rcv_c, tb, &snd_to_c);
    pt_assert("copy send to C for task_b", kr == KERN_SUCCESS);

    /* Build the COPY_SEND message from task_b to task_c's mailbox */
    port_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = snd_to_c;  /* send to task_c's mailbox */
    smsg.hdr.msgh_id          = 2001;
    smsg.body.msgh_descriptor_count = 1;
    smsg.port_desc.type        = MACH_MSG_PORT_DESCRIPTOR;
    smsg.port_desc.name        = snd_b;                  /* task_b's send right to port A */
    smsg.port_desc.disposition = MACH_MSG_TYPE_COPY_SEND;
    smsg.payload = 0xABCD1234u;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    pt_assert("COPY_SEND message sent successfully", kr == KERN_SUCCESS);

    /* task_c receives the message */
    port_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(tc, rcv_c, &rmsg, sizeof(rmsg), &rsz);
    pt_assert("COPY_SEND message received", kr == KERN_SUCCESS);
    pt_assert("MACH_MSGH_BITS_COMPLEX in received hdr",
              (rmsg.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0);
    pt_assert("payload intact", rmsg.payload == 0xABCD1234u);
    pt_assert("port descriptor type correct",
              rmsg.port_desc.type == MACH_MSG_PORT_DESCRIPTOR);

    mach_port_name_t copied_snd = rmsg.port_desc.name;
    pt_assert("received send right name is valid", copied_snd != MACH_PORT_NULL);

    /* Verify task_b still has its original send right (COPY leaves it intact) */
    pt_assert("task_b still has send right after COPY_SEND",
              verify_send_recv(tb, snd_b, ta, rcv_a, 0xBEEF0001u));

    /* Verify task_c can now use the copied send right */
    if (copied_snd != MACH_PORT_NULL) {
        pt_assert("task_c can send via copied right",
                  verify_send_recv(tc, copied_snd, ta, rcv_a, 0xBEEF0002u));
    }

    task_destroy(tc);
    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 2: MOVE_SEND — sender's right is consumed; receiver gets it
 * ------------------------------------------------------------------------- */
static void test_move_send(void)
{
    serial_putstr("\r\n[Port Transfer Test 2] MOVE_SEND\r\n");

    struct task *ta, *tb;
    mach_port_name_t rcv_a, snd_b;
    kern_return_t kr = setup_pair(&ta, &tb, &rcv_a, &snd_b);
    pt_assert("setup port pair (MOVE_SEND)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    struct task *tc = task_create(kernel_task_ptr());
    pt_assert("task_c created", tc != (void *)0);
    if (!tc) { task_destroy(ta); task_destroy(tb); return; }

    mach_port_name_t rcv_c, snd_to_c;
    struct ipc_port  *portC;
    ipc_right_alloc_receive(tc, &rcv_c, &portC, 0);
    ipc_right_copy_send(tc, rcv_c, tb, &snd_to_c);

    port_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = snd_to_c;
    smsg.hdr.msgh_id          = 2002;
    smsg.body.msgh_descriptor_count = 1;
    smsg.port_desc.type        = MACH_MSG_PORT_DESCRIPTOR;
    smsg.port_desc.name        = snd_b;
    smsg.port_desc.disposition = MACH_MSG_TYPE_MOVE_SEND;
    smsg.payload = 0xDEAD5678u;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    pt_assert("MOVE_SEND message sent", kr == KERN_SUCCESS);

    port_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(tc, rcv_c, &rmsg, sizeof(rmsg), &rsz);
    pt_assert("MOVE_SEND message received", kr == KERN_SUCCESS);
    pt_assert("payload intact", rmsg.payload == 0xDEAD5678u);

    mach_port_name_t moved_snd = rmsg.port_desc.name;
    pt_assert("moved send right name is valid", moved_snd != MACH_PORT_NULL);

    /* Verify task_c can now use the moved send right */
    if (moved_snd != MACH_PORT_NULL) {
        pt_assert("task_c can send via moved right",
                  verify_send_recv(tc, moved_snd, ta, rcv_a, 0xCAFE0001u));
    }

    task_destroy(tc);
    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 3: MAKE_SEND — sender holds receive right; makes a send for receiver
 * ------------------------------------------------------------------------- */
static void test_make_send(void)
{
    serial_putstr("\r\n[Port Transfer Test 3] MAKE_SEND\r\n");

    /* task_a creates port P and holds both receive and send rights */
    struct task *ta = task_create(kernel_task_ptr());
    struct task *tc = task_create(kernel_task_ptr());
    pt_assert("tasks created", ta != (void *)0 && tc != (void *)0);
    if (!ta || !tc) {
        if (ta) task_destroy(ta);
        if (tc) task_destroy(tc);
        return;
    }

    /* task_a allocates port P with RECEIVE right */
    mach_port_name_t rcv_a, snd_a2tc;
    struct ipc_port *portA;
    kern_return_t kr = ipc_right_alloc_receive(ta, &rcv_a, &portA, 0);
    pt_assert("alloc port P (receive only)", kr == KERN_SUCCESS);

    /* task_a also needs to be able to send TO task_c's port for the message */
    mach_port_name_t rcv_c, snd_ta_to_c;
    struct ipc_port *portC;
    ipc_right_alloc_receive(tc, &rcv_c, &portC, 0);
    ipc_right_copy_send(tc, rcv_c, ta, &snd_ta_to_c);

    /* task_a sends a message to task_c embedding MAKE_SEND for port P */
    port_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = snd_ta_to_c;
    smsg.hdr.msgh_id          = 2003;
    smsg.body.msgh_descriptor_count = 1;
    smsg.port_desc.type        = MACH_MSG_PORT_DESCRIPTOR;
    smsg.port_desc.name        = rcv_a;               /* task_a's receive right */
    smsg.port_desc.disposition = MACH_MSG_TYPE_MAKE_SEND;
    smsg.payload = 0xF00D0001u;

    kr = mach_msg_send(ta, &smsg.hdr, sizeof(smsg));
    pt_assert("MAKE_SEND message sent", kr == KERN_SUCCESS);

    port_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(tc, rcv_c, &rmsg, sizeof(rmsg), &rsz);
    pt_assert("MAKE_SEND message received", kr == KERN_SUCCESS);
    pt_assert("payload intact (MAKE_SEND)", rmsg.payload == 0xF00D0001u);

    mach_port_name_t made_snd = rmsg.port_desc.name;
    pt_assert("made send right name is valid", made_snd != MACH_PORT_NULL);

    /* Verify task_c can use the made send right to reach task_a's port P */
    if (made_snd != MACH_PORT_NULL) {
        pt_assert("task_c can send to port P via MAKE_SEND right",
                  verify_send_recv(tc, made_snd, ta, rcv_a, 0xF00D0002u));
    }

    (void)snd_a2tc;
    task_destroy(tc);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * Test 4: No-senders notification
 *
 * Registers a notification port on a service port, then destroys all send
 * rights and verifies that a MACH_NOTIFY_NO_SENDERS message was delivered.
 * ------------------------------------------------------------------------- */
static void test_no_senders_notification(void)
{
    serial_putstr("\r\n[Port Transfer Test 4] No-senders notification\r\n");

    struct task *ta = task_create(kernel_task_ptr());
    pt_assert("task_a created", ta != (void *)0);
    if (!ta) return;

    /* Create the service port (receiver = task_a) */
    mach_port_name_t svc_rcv;
    struct ipc_port *svc_port;
    kern_return_t kr = ipc_right_alloc_receive(ta, &svc_rcv, &svc_port, 0);
    pt_assert("service port allocated", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) { task_destroy(ta); return; }

    /* Create a notification port in the same task */
    mach_port_name_t notify_rcv;
    struct ipc_port *notify_port;
    kr = ipc_right_alloc_receive(ta, &notify_rcv, &notify_port, 1 /* also_send */);
    pt_assert("notification port allocated", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) { task_destroy(ta); return; }

    /* Copy a send right on the service port (so it has ip_send_rights > 0) */
    struct task *tb = task_create(kernel_task_ptr());
    mach_port_name_t svc_snd;
    kr = ipc_right_copy_send(ta, svc_rcv, tb, &svc_snd);
    pt_assert("copy send right to task_b", kr == KERN_SUCCESS);

    /* Register no-senders notification on the service port */
    struct ipc_port *prev = (void *)0;
    kr = ipc_right_request_notification(ta, svc_rcv,
                                         ta, notify_rcv,
                                         &prev);
    pt_assert("register no-senders notification", kr == KERN_SUCCESS);

    /* Verify no notification yet (send right still exists) */
    simple_msg_t check_msg;
    mach_msg_size_t csz = 0;
    kr = mach_msg_receive(ta, notify_rcv, &check_msg, sizeof(check_msg), &csz);
    pt_assert("no notification before send rights dropped", kr != KERN_SUCCESS);

    /* Destroy task_b's send right (the only send right) */
    kr = ipc_right_deallocate(tb, svc_snd);
    pt_assert("deallocate last send right", kr == KERN_SUCCESS);

    /* Now the notification should have been delivered */
    kmemset(&check_msg, 0, sizeof(check_msg));
    csz = 0;
    kr = mach_msg_receive(ta, notify_rcv, &check_msg, sizeof(check_msg), &csz);
    pt_assert("no-senders notification received", kr == KERN_SUCCESS);
    pt_assert("msgh_id == MACH_NOTIFY_NO_SENDERS",
              check_msg.hdr.msgh_id == MACH_NOTIFY_NO_SENDERS);

    task_destroy(tb);
    task_destroy(ta);
}

/* -------------------------------------------------------------------------
 * ipc_port_transfer_test_run — entry point
 * ------------------------------------------------------------------------- */

int ipc_port_transfer_test_run(void)
{
    pt_test_count = 0;
    pt_pass_count = 0;
    pt_fail_count = 0;

    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH IPC Port Transfer Test (Phase 2)\r\n");
    serial_putstr("========================================\r\n");

    test_copy_send();
    test_move_send();
    test_make_send();
    test_no_senders_notification();

    serial_putstr("\r\n========================================\r\n");
    serial_putstr(" Port Transfer Results: ");
    serial_putdec(pt_pass_count);
    serial_putstr(" passed, ");
    serial_putdec(pt_fail_count);
    serial_putstr(" failed, ");
    serial_putdec(pt_test_count);
    serial_putstr(" total\r\n");

    if (pt_fail_count == 0) {
        serial_putstr(" STATUS: PASS\r\n");
        serial_putstr("========================================\r\n");
    } else {
        serial_putstr(" STATUS: FAIL\r\n");
        serial_putstr("========================================\r\n");
    }

    return (pt_fail_count > 0) ? 1 : 0;
}
