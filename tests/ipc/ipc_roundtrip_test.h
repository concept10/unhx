/*
 * tests/ipc/ipc_roundtrip_test.h — IPC round-trip correctness test header
 */

#ifndef IPC_ROUNDTRIP_TEST_H
#define IPC_ROUNDTRIP_TEST_H

/*
 * ipc_roundtrip_test_run — execute the IPC round-trip test suite.
 *
 * Tests the following using the ipc_right.h high-level API:
 *   1. Basic send → receive between two tasks
 *   2. Combined SEND|RCV trap (RPC pattern, L4-inspired)
 *   3. Security invariants (send without right, receive without right)
 *   4. Right lifecycle (alloc, deallocate, use-after-free detection)
 *   5. Right transfer between tasks
 *
 * Returns 0 on success, 1 on any failure.
 */
int ipc_roundtrip_test_run(void);

#endif /* IPC_ROUNDTRIP_TEST_H */
