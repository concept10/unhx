/*
 * tests/ipc/ipc_port_transfer_test.h — Port right transfer in messages test
 *
 * Tests the Phase 2 port right transfer support: sending and receiving
 * messages that carry Mach port rights in mach_msg_port_descriptor_t
 * typed descriptors within a MACH_MSGH_BITS_COMPLEX message.
 */

#ifndef IPC_PORT_TRANSFER_TEST_H
#define IPC_PORT_TRANSFER_TEST_H

/*
 * ipc_port_transfer_test_run — execute the port right transfer test suite.
 *
 * Tests:
 *   1. COPY_SEND: sender retains right; receiver gets a new send right.
 *   2. MOVE_SEND: sender's right is consumed; receiver gets the send right.
 *   3. MOVE_SEND_ONCE: send-once right transferred; receiver gets one use.
 *   4. MOVE_RECEIVE: receive right transferred between tasks.
 *   5. MAKE_SEND: sender holds receive right; creates a send right for receiver.
 *   6. Multiple port rights in a single message.
 *   7. Security: invalid disposition rejected.
 *   8. No-senders notification: fires after all send rights destroyed.
 *
 * Returns 0 on success, 1 on any failure.
 */
int ipc_port_transfer_test_run(void);

#endif /* IPC_PORT_TRANSFER_TEST_H */
