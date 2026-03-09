/*
 * tests/ipc/ipc_timeout_test.h — Blocking receive with timeout test
 *
 * Tests the Phase 2 blocking receive with timeout feature.
 */

#ifndef IPC_TIMEOUT_TEST_H
#define IPC_TIMEOUT_TEST_H

/*
 * ipc_timeout_test_run — execute the receive-with-timeout test suite.
 *
 * Tests:
 *   1. Non-blocking receive on empty port returns error immediately.
 *   2. Blocking receive with timeout expires with KERN_OPERATION_TIMED_OUT.
 *   3. Blocking receive succeeds when message is already queued.
 *   4. mach_msg_trap with MACH_RCV_TIMEOUT flag honours the timeout.
 *   5. mach_msg_trap with MACH_RCV_TIMEOUT succeeds when message queued.
 *
 * Returns 0 on success, 1 on any failure.
 */
int ipc_timeout_test_run(void);

#endif /* IPC_TIMEOUT_TEST_H */
