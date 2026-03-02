/*
 * kernel/kern/kernel_task.c — Kernel bootstrap and IPC smoke test for UNHU
 *
 * This file creates the kernel task and implements the Phase 1 milestone
 * test that proves Mach IPC works end-to-end.
 *
 * The test:
 *   1. Creates task_a and task_b
 *   2. Allocates a port in task_a's space with a receive right
 *   3. Grants task_b a send right to that port
 *   4. task_b sends a message containing "hello"
 *   5. task_a receives and verifies the message
 *   6. Prints confirmation to serial console
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 */

#include "kernel_task.h"
#include "task.h"
#include "klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_mqueue.h"

/* Serial output (from platform layer) */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);

/* Defined in task.c */
extern void kernel_task_create(void);

void kernel_task_init(void)
{
    kernel_task_create();

    if (kernel_task_ptr())
        serial_putstr("[UNHU] kernel task (task 0) created\r\n");
    else
        serial_putstr("[UNHU] ERROR: failed to create kernel task\r\n");
}

/*
 * Test message structure: header + a short payload.
 */
struct test_message {
    mach_msg_header_t   header;
    uint32_t            magic;
    char                text[32];
};

void create_test_tasks(void)
{
    serial_putstr("[UNHU IPC] beginning IPC smoke test...\r\n");

    /* Step 1: Create two tasks */
    struct task *task_a = task_create(kernel_task_ptr());
    struct task *task_b = task_create(kernel_task_ptr());

    if (!task_a || !task_b) {
        serial_putstr("[UNHU IPC] FAIL: could not create test tasks\r\n");
        return;
    }
    serial_putstr("[UNHU IPC] task_a and task_b created\r\n");

    /*
     * Step 2: Allocate a port in task_a's space with a RECEIVE right.
     *
     * In Mach, when you create a port, you get the receive right in your
     * own port space.  The receive right is exclusive — only one task
     * holds it at a time.
     */
    struct ipc_space *space_a = task_a->t_ipc_space;
    mach_port_name_t port_name_a;

    ipc_space_lock(space_a);
    kern_return_t kr = ipc_space_alloc_name(space_a, &port_name_a);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(space_a);
        serial_putstr("[UNHU IPC] FAIL: could not allocate port name\r\n");
        return;
    }

    /* Create the kernel port object with task_a as the receiver */
    struct ipc_port *port = ipc_port_alloc(task_a);
    if (!port) {
        ipc_space_unlock(space_a);
        serial_putstr("[UNHU IPC] FAIL: could not allocate port\r\n");
        return;
    }

    /* Install the receive right in task_a's space */
    space_a->is_table[port_name_a].ie_object = port;
    space_a->is_table[port_name_a].ie_bits   = IE_BITS_RECEIVE;

    ipc_space_unlock(space_a);
    serial_putstr("[UNHU IPC] port allocated in task_a (receive right)\r\n");

    /*
     * Step 3: Grant task_b a SEND right to the same port.
     *
     * In real Mach, send rights are transferred via messages (port rights
     * carried in message descriptors).  For this smoke test we do it
     * directly in the kernel, which is equivalent to what the kernel
     * does internally when processing a port right transfer.
     */
    struct ipc_space *space_b = task_b->t_ipc_space;
    mach_port_name_t port_name_b;

    ipc_space_lock(space_b);
    kr = ipc_space_alloc_name(space_b, &port_name_b);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(space_b);
        serial_putstr("[UNHU IPC] FAIL: could not allocate port name in task_b\r\n");
        return;
    }

    /* Install the send right in task_b's space */
    space_b->is_table[port_name_b].ie_object = port;
    space_b->is_table[port_name_b].ie_bits   = IE_BITS_SEND;

    /* Increment the port's send right reference count */
    ipc_port_lock(port);
    port->ip_send_rights++;
    ipc_port_unlock(port);

    ipc_space_unlock(space_b);
    serial_putstr("[UNHU IPC] send right granted to task_b\r\n");

    /*
     * Step 4: task_b sends a message to the port.
     */
    struct test_message send_msg;
    kmemset(&send_msg, 0, sizeof(send_msg));
    send_msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    send_msg.header.msgh_size        = sizeof(send_msg);
    send_msg.header.msgh_remote_port = port_name_b;  /* dest port in task_b's space */
    send_msg.header.msgh_local_port  = MACH_PORT_NULL;
    send_msg.header.msgh_id          = 1;
    send_msg.magic = 0xDEADBEEF;
    kstrncpy(send_msg.text, "hello", sizeof(send_msg.text));

    kr = mach_msg_send(task_b, &send_msg.header, sizeof(send_msg));
    if (kr != KERN_SUCCESS) {
        serial_putstr("[UNHU IPC] FAIL: mach_msg_send returned error ");
        serial_puthex((uint64_t)kr);
        serial_putstr("\r\n");
        return;
    }
    serial_putstr("[UNHU IPC] task_b sent message\r\n");

    /*
     * Step 5: task_a receives the message.
     */
    struct test_message recv_msg;
    mach_msg_size_t recv_size = 0;
    kmemset(&recv_msg, 0, sizeof(recv_msg));

    kr = mach_msg_receive(task_a, port_name_a,
                          &recv_msg, sizeof(recv_msg), &recv_size);
    if (kr != KERN_SUCCESS) {
        serial_putstr("[UNHU IPC] FAIL: mach_msg_receive returned error ");
        serial_puthex((uint64_t)kr);
        serial_putstr("\r\n");
        return;
    }

    /*
     * Step 6: Verify the message contents.
     */
    serial_putstr("[UNHU IPC] message received: ");
    serial_putstr(recv_msg.text);
    serial_putstr("\r\n");

    if (recv_msg.magic == 0xDEADBEEF &&
        kstrcmp(recv_msg.text, "hello") == 0)
    {
        serial_putstr("[UNHU IPC] magic: ");
        serial_puthex((uint64_t)recv_msg.magic);
        serial_putstr(" (correct)\r\n");
        serial_putstr("[UNHU] Phase 1 complete. Mach IPC operational.\r\n");
    } else {
        serial_putstr("[UNHU IPC] FAIL: message contents mismatch\r\n");
        serial_putstr("[UNHU IPC] expected magic=0xDEADBEEF, got ");
        serial_puthex((uint64_t)recv_msg.magic);
        serial_putstr("\r\n");
    }

    /* Clean up */
    task_destroy(task_b);
    task_destroy(task_a);
}
