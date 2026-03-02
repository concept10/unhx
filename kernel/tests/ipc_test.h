/*
 * kernel/tests/ipc_test.h — IPC milestone self-test for UNHOX (v0.2)
 *
 * Controlled by CMake option UNHOX_BOOT_TESTS=ON.
 * Called from kernel_main() in debug builds.
 */

#ifndef IPC_TEST_H
#define IPC_TEST_H

/*
 * ipc_test_run — execute the IPC milestone test.
 *
 * Test procedure:
 *   1. Creates two kernel-mode tasks (task_a, task_b)
 *   2. Allocates a port in task_a with receive right
 *   3. Gives task_b a send right to that port
 *   4. Has task_b send: { magic: 0xDEADBEEF, message: "phase1_ok" }
 *   5. Has task_a receive and verify the magic number and message
 *   6. Prints PASS or FAIL with details to serial console
 *
 * Returns:
 *   0 — all tests passed
 *   1 — one or more tests failed
 */
int ipc_test_run(void);

#endif /* IPC_TEST_H */
