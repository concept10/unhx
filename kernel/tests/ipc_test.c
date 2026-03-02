/*
 * kernel/tests/ipc_test.c — IPC milestone self-test for UNHU (v0.2)
 *
 * This is the formal Phase 1 milestone test.  It exercises the entire IPC
 * path end-to-end: task creation, port allocation, right distribution,
 * message send, and message receive.
 *
 * The test is compiled in only when UNHU_BOOT_TESTS=ON is set in CMake.
 * It is called from kernel_main() and halts with a clear pass/fail exit code.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 */

#include "ipc_test.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* Test message structure */
struct ipc_test_message {
    mach_msg_header_t   header;
    uint32_t            magic;
    char                message[32];
};

static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

static void test_assert(const char *name, int condition)
{
    test_count++;
    if (condition) {
        pass_count++;
        serial_putstr("  [PASS] ");
    } else {
        fail_count++;
        serial_putstr("  [FAIL] ");
    }
    serial_putstr(name);
    serial_putstr("\r\n");
}

int ipc_test_run(void)
{
    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" UNHU IPC Milestone Test (v0.2)\r\n");
    serial_putstr("========================================\r\n");

    /* --- Test 1: Create tasks --- */
    struct task *task_a = task_create(kernel_task_ptr());
    struct task *task_b = task_create(kernel_task_ptr());
    test_assert("task_a created", task_a != (void *)0);
    test_assert("task_b created", task_b != (void *)0);

    if (!task_a || !task_b) {
        serial_putstr("  Cannot continue without tasks.\r\n");
        goto done;
    }

    test_assert("task_a has ipc_space", task_a->t_ipc_space != (void *)0);
    test_assert("task_b has ipc_space", task_b->t_ipc_space != (void *)0);

    /* --- Test 2: Allocate port with receive right --- */
    struct ipc_space *space_a = task_a->t_ipc_space;
    mach_port_name_t port_name_a;

    ipc_space_lock(space_a);
    kern_return_t kr = ipc_space_alloc_name(space_a, &port_name_a);
    test_assert("alloc port name in task_a", kr == KERN_SUCCESS);

    struct ipc_port *port = ipc_port_alloc(task_a);
    test_assert("port object allocated", port != (void *)0);

    if (kr != KERN_SUCCESS || !port) {
        ipc_space_unlock(space_a);
        goto cleanup;
    }

    space_a->is_table[port_name_a].ie_object = port;
    space_a->is_table[port_name_a].ie_bits   = IE_BITS_RECEIVE;
    ipc_space_unlock(space_a);

    /* --- Test 3: Grant send right to task_b --- */
    struct ipc_space *space_b = task_b->t_ipc_space;
    mach_port_name_t port_name_b;

    ipc_space_lock(space_b);
    kr = ipc_space_alloc_name(space_b, &port_name_b);
    test_assert("alloc port name in task_b", kr == KERN_SUCCESS);

    if (kr == KERN_SUCCESS) {
        space_b->is_table[port_name_b].ie_object = port;
        space_b->is_table[port_name_b].ie_bits   = IE_BITS_SEND;

        ipc_port_lock(port);
        port->ip_send_rights++;
        ipc_port_unlock(port);
    }
    ipc_space_unlock(space_b);

    /* --- Test 4: Send message { magic: 0xDEADBEEF, message: "phase1_ok" } --- */
    struct ipc_test_message send_msg;
    kmemset(&send_msg, 0, sizeof(send_msg));
    send_msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    send_msg.header.msgh_size        = sizeof(send_msg);
    send_msg.header.msgh_remote_port = port_name_b;
    send_msg.header.msgh_local_port  = MACH_PORT_NULL;
    send_msg.header.msgh_id          = 42;
    send_msg.magic = 0xDEADBEEF;
    kstrncpy(send_msg.message, "phase1_ok", sizeof(send_msg.message));

    kr = mach_msg_send(task_b, &send_msg.header, sizeof(send_msg));
    test_assert("mach_msg_send returns KERN_SUCCESS", kr == KERN_SUCCESS);

    /* --- Test 5: Receive and verify --- */
    struct ipc_test_message recv_msg;
    mach_msg_size_t recv_size = 0;
    kmemset(&recv_msg, 0, sizeof(recv_msg));

    kr = mach_msg_receive(task_a, port_name_a,
                          &recv_msg, sizeof(recv_msg), &recv_size);
    test_assert("mach_msg_receive returns KERN_SUCCESS", kr == KERN_SUCCESS);
    test_assert("received magic == 0xDEADBEEF", recv_msg.magic == 0xDEADBEEF);
    test_assert("received message == \"phase1_ok\"",
                kstrcmp(recv_msg.message, "phase1_ok") == 0);
    test_assert("received size matches sent size",
                recv_size == sizeof(send_msg));

    /* Print received values for debugging */
    serial_putstr("  received magic: ");
    serial_puthex((uint64_t)recv_msg.magic);
    serial_putstr("\r\n");
    serial_putstr("  received text:  ");
    serial_putstr(recv_msg.message);
    serial_putstr("\r\n");

    /* --- Test 6: Security — send without right should fail --- */
    struct task *task_c = task_create(kernel_task_ptr());
    if (task_c) {
        /* task_c has no send right to the port */
        struct ipc_test_message bad_msg;
        kmemset(&bad_msg, 0, sizeof(bad_msg));
        bad_msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
        bad_msg.header.msgh_size        = sizeof(bad_msg);
        bad_msg.header.msgh_remote_port = 1; /* nonexistent port name */
        bad_msg.header.msgh_local_port  = MACH_PORT_NULL;
        bad_msg.header.msgh_id          = 99;
        bad_msg.magic = 0xBADBAD;
        kstrncpy(bad_msg.message, "unauthorized", sizeof(bad_msg.message));

        kr = mach_msg_send(task_c, &bad_msg.header, sizeof(bad_msg));
        test_assert("send without right returns error", kr != KERN_SUCCESS);
        task_destroy(task_c);
    }

cleanup:
    task_destroy(task_b);
    task_destroy(task_a);

done:
    /* --- Summary --- */
    serial_putstr("========================================\r\n");
    serial_putstr(" Results: ");
    serial_putdec(pass_count);
    serial_putstr(" passed, ");
    serial_putdec(fail_count);
    serial_putstr(" failed, ");
    serial_putdec(test_count);
    serial_putstr(" total\r\n");

    if (fail_count == 0) {
        serial_putstr(" STATUS: PASS\r\n");
        serial_putstr("========================================\r\n");
        serial_putstr("[UNHU] IPC milestone v0.2 PASSED.\r\n");
    } else {
        serial_putstr(" STATUS: FAIL\r\n");
        serial_putstr("========================================\r\n");
        serial_putstr("[UNHU] IPC milestone v0.2 FAILED.\r\n");
    }

    return (fail_count > 0) ? 1 : 0;
}
