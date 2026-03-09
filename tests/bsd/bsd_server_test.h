/*
 * tests/bsd/bsd_server_test.h — BSD server test suite for NEOMACH
 */

#ifndef BSD_SERVER_TEST_H
#define BSD_SERVER_TEST_H

/*
 * bsd_server_test_run — run all BSD server self-tests.
 *
 * Reports results via serial_putstr() in the same PASS/FAIL format
 * as the IPC test suite (tests/ipc/ipc_roundtrip_test.c).
 *
 * Returns 0 if all tests passed, non-zero if any test failed.
 * Called from kernel_main() when NEOMACH_BOOT_TESTS=ON.
 */
int bsd_server_test_run(void);

#endif /* BSD_SERVER_TEST_H */
