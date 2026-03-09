/*
 * tests/ipc/ipc_ool_test.c — Out-of-line memory descriptor IPC test
 *
 * Exercises the Phase 2 OOL memory descriptor support.  Complex messages
 * (MACH_MSGH_BITS_COMPLEX) carrying mach_msg_ool_descriptor_t entries
 * allow large payloads to be transferred without being embedded inline in
 * the message body.
 *
 * In Phase 2 the kernel physically copies OOL data into a kalloc'd region
 * and delivers the kernel pointer to the receiver.  Phase 3 will use
 * vm_map-level zero-copy instead.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.4 — Messages;
 *            OSF MK ipc/mach_msg.c for OOL handling.
 */

#include "ipc_ool_test.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "kern/kalloc.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "mach/mach_types.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_putdec(uint32_t val);
extern void serial_puthex(uint64_t val);

/* -------------------------------------------------------------------------
 * Test framework helpers (mirrored from ipc_roundtrip_test.c)
 * ------------------------------------------------------------------------- */

static int ool_test_count = 0;
static int ool_pass_count = 0;
static int ool_fail_count = 0;

static void ool_assert(const char *name, int cond)
{
    ool_test_count++;
    if (cond) {
        ool_pass_count++;
        serial_putstr("  [PASS] ");
    } else {
        ool_fail_count++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

/* -------------------------------------------------------------------------
 * Message types
 * ------------------------------------------------------------------------- */

/*
 * ool_msg_t — a complex message carrying one OOL buffer.
 *
 * Layout:
 *   mach_msg_header_t   (24 bytes)
 *   mach_msg_body_t     ( 4 bytes; descriptor_count = 1)
 *   mach_msg_ool_descriptor_t (20 bytes on LP64)
 *   uint32_t inline_magic (4 bytes)  — sanity-check inline data still works
 */
typedef struct {
    mach_msg_header_t           hdr;
    mach_msg_body_t             body;
    mach_msg_ool_descriptor_t   ool;
    uint32_t                    inline_magic;
} ool_msg_t;

/*
 * ool_multi_msg_t — a complex message carrying two OOL buffers.
 */
typedef struct {
    mach_msg_header_t           hdr;
    mach_msg_body_t             body;
    mach_msg_ool_descriptor_t   ool0;
    mach_msg_ool_descriptor_t   ool1;
} ool_multi_msg_t;

/* -------------------------------------------------------------------------
 * Helper: set up a two-task port pair
 *
 * Creates task_a (receiver) and task_b (sender) and gives task_b a SEND
 * right to a port owned by task_a.
 *
 * Returns KERN_SUCCESS on success; fills *port_a and *port_b_name.
 * ------------------------------------------------------------------------- */
static kern_return_t
setup_port_pair(struct task **ta_out, struct task **tb_out,
                mach_port_name_t *recv_name_out,
                mach_port_name_t *send_name_out)
{
    struct task *ta = task_create(kernel_task_ptr());
    struct task *tb = task_create(kernel_task_ptr());
    if (!ta || !tb) return KERN_RESOURCE_SHORTAGE;

    mach_port_name_t rcv_name, snd_name;
    struct ipc_port  *port;

    kern_return_t kr = ipc_right_alloc_receive(ta, &rcv_name, &port, 0);
    if (kr != KERN_SUCCESS) goto fail;

    kr = ipc_right_copy_send(ta, rcv_name, tb, &snd_name);
    if (kr != KERN_SUCCESS) goto fail;

    *ta_out        = ta;
    *tb_out        = tb;
    *recv_name_out = rcv_name;
    *send_name_out = snd_name;
    return KERN_SUCCESS;

fail:
    task_destroy(ta);
    task_destroy(tb);
    return kr;
}

/* -------------------------------------------------------------------------
 * Test 1: Single OOL buffer round-trip
 * ------------------------------------------------------------------------- */
static void test_ool_single(void)
{
    serial_putstr("\r\n[OOL Test 1] Single OOL buffer round-trip\r\n");

    struct task *ta, *tb;
    mach_port_name_t recv_port, send_port;
    kern_return_t kr = setup_port_pair(&ta, &tb, &recv_port, &send_port);
    ool_assert("setup_port_pair", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    /* Allocate an OOL buffer with a recognizable pattern */
    const mach_msg_size_t OOL_SIZE = 64;
    uint8_t *src_buf = (uint8_t *)kalloc(OOL_SIZE);
    ool_assert("OOL source buffer allocated", src_buf != (void *)0);
    if (!src_buf) { task_destroy(ta); task_destroy(tb); return; }

    for (mach_msg_size_t i = 0; i < OOL_SIZE; i++)
        src_buf[i] = (uint8_t)(i ^ 0xA5);

    /* Build the complex send message */
    ool_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = send_port;
    smsg.hdr.msgh_local_port  = MACH_PORT_NULL;
    smsg.hdr.msgh_id          = 1001;

    smsg.body.msgh_descriptor_count = 1;

    smsg.ool.type       = MACH_MSG_OOL_DESCRIPTOR;
    smsg.ool.size       = OOL_SIZE;
    smsg.ool.address    = src_buf;
    smsg.ool.deallocate = 0;
    smsg.ool.copy       = MACH_MSG_PHYSICAL_COPY;

    smsg.inline_magic = 0xC0FFEE01u;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    ool_assert("mach_msg_send complex (OOL) succeeds", kr == KERN_SUCCESS);

    /* Receive */
    ool_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(ta, recv_port, &rmsg, sizeof(rmsg), &rsz);
    ool_assert("mach_msg_receive complex (OOL) succeeds", kr == KERN_SUCCESS);
    ool_assert("received size correct", rsz == sizeof(smsg));
    ool_assert("MACH_MSGH_BITS_COMPLEX set in received header",
               (rmsg.hdr.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0);
    ool_assert("inline_magic preserved",
               rmsg.inline_magic == 0xC0FFEE01u);
    ool_assert("OOL descriptor type correct",
               rmsg.ool.type == MACH_MSG_OOL_DESCRIPTOR);
    ool_assert("OOL size correct", rmsg.ool.size == OOL_SIZE);
    ool_assert("OOL address non-NULL", rmsg.ool.address != (void *)0);

    if (rmsg.ool.address) {
        const uint8_t *dst = (const uint8_t *)rmsg.ool.address;
        int data_ok = 1;
        for (mach_msg_size_t i = 0; i < OOL_SIZE; i++) {
            if (dst[i] != (uint8_t)(i ^ 0xA5)) { data_ok = 0; break; }
        }
        ool_assert("OOL data contents correct", data_ok);

        /* Free the received OOL buffer */
        kfree(rmsg.ool.address);
    }

    kfree(src_buf);
    task_destroy(ta);
    task_destroy(tb);
}

/* -------------------------------------------------------------------------
 * Test 2: Multiple OOL buffers in one message
 * ------------------------------------------------------------------------- */
static void test_ool_multi(void)
{
    serial_putstr("\r\n[OOL Test 2] Multiple OOL buffers in one message\r\n");

    struct task *ta, *tb;
    mach_port_name_t recv_port, send_port;
    kern_return_t kr = setup_port_pair(&ta, &tb, &recv_port, &send_port);
    ool_assert("setup_port_pair (multi)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    const mach_msg_size_t SZ0 = 32, SZ1 = 48;
    uint8_t *buf0 = (uint8_t *)kalloc(SZ0);
    uint8_t *buf1 = (uint8_t *)kalloc(SZ1);
    ool_assert("OOL buffers allocated", buf0 != (void *)0 && buf1 != (void *)0);
    if (!buf0 || !buf1) {
        if (buf0) kfree(buf0);
        if (buf1) kfree(buf1);
        task_destroy(ta); task_destroy(tb);
        return;
    }
    for (mach_msg_size_t i = 0; i < SZ0; i++) buf0[i] = (uint8_t)(i + 10);
    for (mach_msg_size_t i = 0; i < SZ1; i++) buf1[i] = (uint8_t)(i + 20);

    ool_multi_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = send_port;
    smsg.hdr.msgh_id          = 1002;

    smsg.body.msgh_descriptor_count = 2;

    smsg.ool0.type = MACH_MSG_OOL_DESCRIPTOR;
    smsg.ool0.size = SZ0; smsg.ool0.address = buf0;
    smsg.ool0.copy = MACH_MSG_PHYSICAL_COPY;

    smsg.ool1.type = MACH_MSG_OOL_DESCRIPTOR;
    smsg.ool1.size = SZ1; smsg.ool1.address = buf1;
    smsg.ool1.copy = MACH_MSG_PHYSICAL_COPY;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    ool_assert("send multi-OOL message", kr == KERN_SUCCESS);

    ool_multi_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(ta, recv_port, &rmsg, sizeof(rmsg), &rsz);
    ool_assert("receive multi-OOL message", kr == KERN_SUCCESS);

    ool_assert("OOL0 address non-NULL", rmsg.ool0.address != (void *)0);
    ool_assert("OOL1 address non-NULL", rmsg.ool1.address != (void *)0);
    ool_assert("OOL0 size correct", rmsg.ool0.size == SZ0);
    ool_assert("OOL1 size correct", rmsg.ool1.size == SZ1);

    if (rmsg.ool0.address && rmsg.ool1.address) {
        const uint8_t *d0 = (const uint8_t *)rmsg.ool0.address;
        const uint8_t *d1 = (const uint8_t *)rmsg.ool1.address;
        int ok0 = 1, ok1 = 1;
        for (mach_msg_size_t i = 0; i < SZ0; i++)
            if (d0[i] != (uint8_t)(i + 10)) { ok0 = 0; break; }
        for (mach_msg_size_t i = 0; i < SZ1; i++)
            if (d1[i] != (uint8_t)(i + 20)) { ok1 = 0; break; }
        ool_assert("OOL0 data correct", ok0);
        ool_assert("OOL1 data correct", ok1);
        kfree(rmsg.ool0.address);
        kfree(rmsg.ool1.address);
    }

    kfree(buf0);
    kfree(buf1);
    task_destroy(ta);
    task_destroy(tb);
}

/* -------------------------------------------------------------------------
 * Test 3: OOL buffer with zero size (edge case)
 * ------------------------------------------------------------------------- */
static void test_ool_zero_size(void)
{
    serial_putstr("\r\n[OOL Test 3] Zero-size OOL buffer edge case\r\n");

    struct task *ta, *tb;
    mach_port_name_t recv_port, send_port;
    kern_return_t kr = setup_port_pair(&ta, &tb, &recv_port, &send_port);
    ool_assert("setup_port_pair (zero-size)", kr == KERN_SUCCESS);
    if (kr != KERN_SUCCESS) return;

    ool_msg_t smsg;
    kmemset(&smsg, 0, sizeof(smsg));
    smsg.hdr.msgh_bits        = MACH_MSGH_BITS_COMPLEX |
                                 MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    smsg.hdr.msgh_size        = sizeof(smsg);
    smsg.hdr.msgh_remote_port = send_port;
    smsg.hdr.msgh_id          = 1003;
    smsg.body.msgh_descriptor_count = 1;
    smsg.ool.type       = MACH_MSG_OOL_DESCRIPTOR;
    smsg.ool.size       = 0;
    smsg.ool.address    = (void *)0;
    smsg.inline_magic   = 0xDEADC0DEu;

    kr = mach_msg_send(tb, &smsg.hdr, sizeof(smsg));
    ool_assert("send zero-size OOL message", kr == KERN_SUCCESS);

    ool_msg_t rmsg;
    mach_msg_size_t rsz = 0;
    kmemset(&rmsg, 0, sizeof(rmsg));

    kr = mach_msg_receive(ta, recv_port, &rmsg, sizeof(rmsg), &rsz);
    ool_assert("receive zero-size OOL message", kr == KERN_SUCCESS);
    ool_assert("inline_magic still correct", rmsg.inline_magic == 0xDEADC0DEu);
    ool_assert("zero-size OOL size == 0", rmsg.ool.size == 0);

    task_destroy(ta);
    task_destroy(tb);
}

/* -------------------------------------------------------------------------
 * ipc_ool_test_run — entry point
 * ------------------------------------------------------------------------- */

int ipc_ool_test_run(void)
{
    ool_test_count = 0;
    ool_pass_count = 0;
    ool_fail_count = 0;

    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH IPC OOL Test (Phase 2)\r\n");
    serial_putstr("========================================\r\n");

    test_ool_single();
    test_ool_multi();
    test_ool_zero_size();

    serial_putstr("\r\n========================================\r\n");
    serial_putstr(" OOL Results: ");
    serial_putdec(ool_pass_count);
    serial_putstr(" passed, ");
    serial_putdec(ool_fail_count);
    serial_putstr(" failed, ");
    serial_putdec(ool_test_count);
    serial_putstr(" total\r\n");

    if (ool_fail_count == 0) {
        serial_putstr(" STATUS: PASS\r\n");
        serial_putstr("========================================\r\n");
    } else {
        serial_putstr(" STATUS: FAIL\r\n");
        serial_putstr("========================================\r\n");
    }

    return (ool_fail_count > 0) ? 1 : 0;
}
