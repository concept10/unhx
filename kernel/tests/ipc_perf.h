/*
 * kernel/tests/ipc_perf.h — IPC performance baseline test for UNHOX
 *
 * Measures the cost of Mach IPC send and receive operations using the
 * x86-64 TSC (Time Stamp Counter).
 *
 * Controlled by CMake option UNHOX_BOOT_TESTS=ON.
 * Called from kernel_main() after the milestone test.
 */

#ifndef IPC_PERF_H
#define IPC_PERF_H

/*
 * ipc_perf_run — execute the IPC performance benchmark.
 *
 * Measures:
 *   1. Null message send cost (TSC cycles)
 *   2. Null message receive cost (TSC cycles)
 *   3. Send+receive round-trip cost (TSC cycles)
 *
 * Prints results to serial console.
 *
 * Returns:
 *   0 — benchmark completed
 *   1 — setup failed
 */
int ipc_perf_run(void);

#endif /* IPC_PERF_H */
