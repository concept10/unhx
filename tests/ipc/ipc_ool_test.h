/*
 * tests/ipc/ipc_ool_test.h — Out-of-line memory descriptor IPC test
 *
 * Tests the Phase 2 OOL memory descriptor support: sending and receiving
 * messages that carry out-of-line buffers in mach_msg_ool_descriptor_t
 * typed descriptors within a MACH_MSGH_BITS_COMPLEX message.
 */

#ifndef IPC_OOL_TEST_H
#define IPC_OOL_TEST_H

/*
 * ipc_ool_test_run — execute the OOL memory descriptor test suite.
 *
 * Tests:
 *   1. Send a complex message with a single OOL buffer; verify contents.
 *   2. Send a complex message with multiple OOL buffers; verify each.
 *   3. Send an OOL buffer with zero size (edge case).
 *   4. Inline data and OOL buffer in the same message.
 *   5. Security: OOL data from one task is not visible to a third task.
 *
 * Returns 0 on success, 1 on any failure.
 */
int ipc_ool_test_run(void);

#endif /* IPC_OOL_TEST_H */
