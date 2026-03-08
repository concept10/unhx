/*
 * tests/ipc/ipc_perf.h — IPC performance benchmark header for UNHOX
 */

#ifndef IPC_PERF_H
#define IPC_PERF_H

#include <stdint.h>

/*
 * struct ipc_perf_result — result of one benchmark run.
 */
struct ipc_perf_result {
    uint32_t    iterations;             /* number of round-trips measured     */
    uint64_t    total_cycles;           /* total TSC cycles for all trips     */
    uint64_t    cycles_per_roundtrip;   /* average cycles per round-trip      */
    int         error;                  /* non-zero if benchmark aborted      */
};

/*
 * ipc_perf_null_roundtrip — measure N null-message round-trips.
 *
 * Sets up two kernel tasks with a port pair, then sends and receives a
 * header-only (null payload) Mach message N times, recording TSC timestamps.
 *
 * Returns an ipc_perf_result with cycle counts.
 */
struct ipc_perf_result ipc_perf_null_roundtrip(uint32_t iterations);

/*
 * ipc_perf_report — print a benchmark result to the serial console.
 */
void ipc_perf_report(const struct ipc_perf_result *r);

/*
 * ipc_perf_run — run the full benchmark suite and print results.
 *
 * Runs at multiple iteration counts (10, 100, 1000) to show cost amortisation.
 * Results should be recorded in docs/research/ipc-performance.md.
 */
void ipc_perf_run(void);

#endif /* IPC_PERF_H */
