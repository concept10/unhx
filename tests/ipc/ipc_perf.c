/*
 * tests/ipc/ipc_perf.c — Mach IPC round-trip performance benchmark for NEOMACH
 *
 * This benchmark measures the null Mach message round-trip latency — the time
 * from a task sending an empty (null payload) message until the same task has
 * received the reply.  This is the standard IPC micro-benchmark used since the
 * L4 performance papers to characterize microkernel IPC overhead.
 *
 * =========================================================================
 * BENCHMARK DESIGN — INFLUENCED BY L4 PERFORMANCE METHODOLOGY
 * =========================================================================
 *
 * The canonical IPC benchmark since Liedtke (1993) is the "null IPC round-trip":
 *
 *   1. Two tasks (client, server) share a port pair.
 *   2. Client sends a minimal message (header only, no payload) to the server.
 *   3. Server receives it and immediately sends a reply.
 *   4. Client receives the reply.
 *   5. The elapsed wall-clock time for steps 2-4 is the round-trip latency.
 *
 * L4/seL4 achieve round-trip times of ~1 µs on modern x86 hardware.
 * GNU Mach (as used by HURD) historically achieves ~10-30 µs.
 * Linux pipe round-trip is ~4-8 µs (different semantics, lower bound reference).
 *
 * NEOMACH Phase 1 measurements run under QEMU (KVM-accelerated or TCG):
 *   - QEMU/KVM is ~2-5x slower than bare metal for IPC-heavy workloads.
 *   - QEMU/TCG (software emulation) is ~10-50x slower.
 *   - Measurements are recorded in docs/research/ipc-performance.md.
 *
 * =========================================================================
 * TIMING MECHANISM
 * =========================================================================
 *
 * On x86-64, the TSC (Time Stamp Counter) is the highest-resolution timer
 * available in the kernel without involving the APIC or HPET:
 *
 *   uint64_t t0 = rdtsc();
 *   [IPC operation]
 *   uint64_t t1 = rdtsc();
 *   uint64_t cycles = t1 - t0;
 *
 * Cycle count is converted to nanoseconds using the TSC frequency, which
 * on modern out-of-order processors with a constant TSC is equal to the
 * nominal CPU frequency.
 *
 * Phase 1 limitation: we do not calibrate the TSC (requires either CPUID
 * extended info or a wait loop against the PIT/HPET).  We record raw cycle
 * counts and note the CPU frequency in the benchmark report.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986);
 *            Liedtke, "Improving IPC by Kernel Design" (SOSP 1993);
 *            Liedtke, "On µ-Kernel Construction" (SOSP 1995);
 *            Härtig et al., "The Performance of µ-Kernel-Based Systems" (SOSP 1997);
 *            Heiser & Leslie, "The seL4 Microkernel — An Introduction" (2010).
 */

#include "ipc_perf.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "ipc/mach_msg.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* -------------------------------------------------------------------------
 * TSC read helper (x86-64)
 * ------------------------------------------------------------------------- */

static inline uint64_t rdtsc(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    /*
     * Use lfence before rdtsc to serialize instruction execution and prevent
     * out-of-order reads from distorting the measured interval.
     * lfence ensures all prior loads complete before the TSC is read.
     */
    __asm__ volatile ("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
#else
    /* Non-x86: return a monotonic counter placeholder */
    static uint64_t counter = 0;
    return ++counter;
#endif
}

/* -------------------------------------------------------------------------
 * Benchmark message: null payload (header only)
 * ------------------------------------------------------------------------- */

typedef struct {
    mach_msg_header_t hdr;
} null_msg_t;

/* -------------------------------------------------------------------------
 * ipc_perf_null_roundtrip
 *
 * Measures the elapsed TSC cycles for N null-message round-trips.
 *
 * A "round-trip" is the complete request+reply cycle:
 *   1. task_b sends a null request to task_a (enqueue on a's receive port)
 *   2. task_a receives the request
 *   3. task_a sends a null reply to task_b (enqueue on b's receive port)
 *   4. task_b receives the reply
 *
 * This matches the standard IPC micro-benchmark used in the L4 performance
 * literature: both the send path and the return path are measured, giving
 * the true round-trip cost.
 *
 * In a full system (Phase 2+) each step is a syscall + possible context
 * switch.  Phase 1 measures only the in-kernel enqueue/dequeue overhead,
 * establishing the theoretical floor.
 * ------------------------------------------------------------------------- */
struct ipc_perf_result
ipc_perf_null_roundtrip(uint32_t iterations)
{
    struct ipc_perf_result r;
    kmemset(&r, 0, sizeof(r));
    r.iterations = iterations;

    /* Create a pair of tasks */
    struct task *task_a = task_create(kernel_task_ptr());
    struct task *task_b = task_create(kernel_task_ptr());

    if (!task_a || !task_b) {
        r.error = 1;
        if (task_b) task_destroy(task_b);
        if (task_a) task_destroy(task_a);
        return r;
    }

    /* task_a: receive right for requests; task_b: send right to task_a */
    mach_port_name_t rcv_name_a;
    kern_return_t kr = ipc_right_alloc_receive(task_a, &rcv_name_a,
                                               (void *)0, 1);
    if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

    mach_port_name_t snd_to_a;
    kr = ipc_right_copy_send(task_a, rcv_name_a, task_b, &snd_to_a);
    if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

    /* task_b: receive right for replies; task_a: send right to task_b */
    mach_port_name_t rcv_name_b;
    kr = ipc_right_alloc_receive(task_b, &rcv_name_b,
                                 (void *)0, 1);
    if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

    mach_port_name_t snd_to_b;
    kr = ipc_right_copy_send(task_b, rcv_name_b, task_a, &snd_to_b);
    if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

    /* Pre-build null request and reply message headers */
    null_msg_t req_msg;
    kmemset(&req_msg, 0, sizeof(req_msg));
    req_msg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    req_msg.hdr.msgh_size        = sizeof(req_msg);
    req_msg.hdr.msgh_remote_port = snd_to_a;
    req_msg.hdr.msgh_local_port  = MACH_PORT_NULL;
    req_msg.hdr.msgh_id          = 0;

    null_msg_t rep_msg;
    kmemset(&rep_msg, 0, sizeof(rep_msg));
    rep_msg.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    rep_msg.hdr.msgh_size        = sizeof(rep_msg);
    rep_msg.hdr.msgh_remote_port = snd_to_b;
    rep_msg.hdr.msgh_local_port  = MACH_PORT_NULL;
    rep_msg.hdr.msgh_id          = 0;

    null_msg_t recv_a, recv_b;

    /* Warm-up: 8 iterations to populate instruction and data caches */
    for (uint32_t i = 0; i < 8; i++) {
        /* Request: B → A */
        kr = mach_msg_send(task_b, &req_msg.hdr, sizeof(req_msg));
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }
        mach_msg_size_t sz = 0;
        kr = mach_msg_receive(task_a, rcv_name_a, &recv_a, sizeof(recv_a), &sz);
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }
        /* Reply: A → B */
        kr = mach_msg_send(task_a, &rep_msg.hdr, sizeof(rep_msg));
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }
        sz = 0;
        kr = mach_msg_receive(task_b, rcv_name_b, &recv_b, sizeof(recv_b), &sz);
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }
    }

    /* Timed measurement */
    uint64_t t_start = rdtsc();

    for (uint32_t i = 0; i < iterations; i++) {
        req_msg.hdr.msgh_id = (mach_msg_id_t)(2 * i);
        rep_msg.hdr.msgh_id = (mach_msg_id_t)(2 * i + 1);

        /* Request: B → A */
        kr = mach_msg_send(task_b, &req_msg.hdr, sizeof(req_msg));
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

        mach_msg_size_t sz = 0;
        kmemset(&recv_a, 0, sizeof(recv_a));
        kr = mach_msg_receive(task_a, rcv_name_a,
                              &recv_a, sizeof(recv_a), &sz);
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

        /* Reply: A → B */
        kr = mach_msg_send(task_a, &rep_msg.hdr, sizeof(rep_msg));
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }

        sz = 0;
        kmemset(&recv_b, 0, sizeof(recv_b));
        kr = mach_msg_receive(task_b, rcv_name_b,
                              &recv_b, sizeof(recv_b), &sz);
        if (kr != KERN_SUCCESS) { r.error = 1; goto cleanup; }
    }

    uint64_t t_end = rdtsc();

    r.total_cycles = t_end - t_start;
    r.cycles_per_roundtrip =
        (iterations > 0) ? (r.total_cycles / iterations) : 0;

cleanup:
    /* Destroy task_b before task_a: task_a holds a send right to task_b's port.
     * Destroying task_b first frees its receive port, then task_a can safely
     * release its send right to that (now-dead) port during teardown. */
    task_destroy(task_b);
    task_destroy(task_a);
    return r;
}

/* -------------------------------------------------------------------------
 * ipc_perf_report
 *
 * Print benchmark results to the serial console.
 * ------------------------------------------------------------------------- */
void ipc_perf_report(const struct ipc_perf_result *r)
{
    if (!r) return;

    serial_putstr("  iterations:       ");
    serial_putdec(r->iterations);
    serial_putstr("\r\n");

    if (r->error) {
        serial_putstr("  ERROR: benchmark aborted\r\n");
        return;
    }

    serial_putstr("  total cycles:     ");
    serial_puthex(r->total_cycles);
    serial_putstr("\r\n");

    serial_putstr("  cycles/roundtrip (hex): ");
    serial_puthex(r->cycles_per_roundtrip);
    serial_putstr("\r\n");

    /*
     * Print the cycle count in decimal (serial_putdec takes uint32_t;
     * split the 64-bit value into high and low halves for display).
     */
    serial_putstr("  cycles/roundtrip (dec): ");
    uint32_t hi = (uint32_t)(r->cycles_per_roundtrip >> 32);
    uint32_t lo = (uint32_t)(r->cycles_per_roundtrip & 0xFFFFFFFFu);
    if (hi) {
        serial_putdec(hi);
        serial_putstr("_");
    }
    serial_putdec(lo);
    serial_putstr(" cycles\r\n");

    serial_putstr("  (see docs/research/ipc-performance.md for analysis)\r\n");
}

/* -------------------------------------------------------------------------
 * ipc_perf_run — top-level benchmark entry point
 * ------------------------------------------------------------------------- */
void ipc_perf_run(void)
{
    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" NEOMACH IPC Performance Benchmark\r\n");
    serial_putstr(" Null Mach message round-trip latency\r\n");
    serial_putstr("========================================\r\n");

    /* Run at three scales to show amortisation */
    static const uint32_t scales[] = { 10, 100, 1000 };
    static const uint32_t nscales  = 3;

    for (uint32_t s = 0; s < nscales; s++) {
        uint32_t N = scales[s];
        serial_putstr("\r\n--- N = ");
        serial_putdec(N);
        serial_putstr(" ---\r\n");

        struct ipc_perf_result r = ipc_perf_null_roundtrip(N);
        ipc_perf_report(&r);
    }

    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" Benchmark complete.\r\n");
    serial_putstr(" Record results in docs/research/ipc-performance.md\r\n");
    serial_putstr("========================================\r\n");
}