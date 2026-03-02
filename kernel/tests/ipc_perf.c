/*
 * kernel/tests/ipc_perf.c — IPC performance baseline for UNHOX
 *
 * Measures the cost of Mach IPC operations using the x86-64 TSC
 * (Time Stamp Counter).  This gives cycle-accurate measurements
 * independent of timer configuration.
 *
 * The benchmark sends N null messages (minimal header-only payload) and
 * reports min/avg/max cycle counts for:
 *   - mach_msg_send (enqueue a message)
 *   - mach_msg_receive (dequeue a message)
 *   - round-trip (send + receive)
 *
 * Phase 1 notes:
 *   - Non-blocking IPC only (no thread wakeup overhead)
 *   - Copy semantics (message copied into kernel buffer)
 *   - Single-threaded execution (no lock contention)
 *   - kfree is a no-op (no deallocation overhead)
 *   These results represent a LOWER BOUND for Phase 2+ where blocking,
 *   contention, and real deallocation will add overhead.
 *
 * Reference: Liedtke, "Improving IPC by Kernel Design" (SOSP 1993)
 */

#include "ipc_perf.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"

/* Serial output */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/*
 * Number of iterations for each benchmark.
 *
 * Phase 1 constraint: kfree() is a no-op, so every ipc_kmsg allocated
 * during send (~1040 bytes each) is permanently leaked.  With 256 KB
 * heap and prior allocations from the smoke/milestone tests, we can
 * safely run ~100 iterations across all three benchmarks (~300 total
 * allocations = ~300 KB, within budget).
 */
#define PERF_ITERATIONS  50

/* Read the TSC (Time Stamp Counter) */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Minimal message: just a header */
struct perf_message {
    mach_msg_header_t header;
    uint32_t          seq;
};

static void print_stats(const char *label, uint64_t min, uint64_t max,
                         uint64_t total, uint32_t count)
{
    uint64_t avg = total / count;

    serial_putstr("  ");
    serial_putstr(label);
    serial_putstr(":  min=");
    serial_putdec((uint32_t)min);
    serial_putstr("  avg=");
    serial_putdec((uint32_t)avg);
    serial_putstr("  max=");
    serial_putdec((uint32_t)max);
    serial_putstr(" cycles  (n=");
    serial_putdec(count);
    serial_putstr(")\r\n");
}

int ipc_perf_run(void)
{
    serial_putstr("\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr(" UNHOX IPC Performance Baseline\r\n");
    serial_putstr("========================================\r\n");

    /* --- Setup: create sender/receiver tasks and port --- */
    struct task *sender = task_create(kernel_task_ptr());
    struct task *receiver = task_create(kernel_task_ptr());
    if (!sender || !receiver) {
        serial_putstr("  FAIL: could not create tasks\r\n");
        return 1;
    }

    /* Allocate port in receiver's space with receive right */
    struct ipc_space *r_space = receiver->t_ipc_space;
    mach_port_name_t r_port_name;

    ipc_space_lock(r_space);
    kern_return_t kr = ipc_space_alloc_name(r_space, &r_port_name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(r_space);
        serial_putstr("  FAIL: could not allocate port name\r\n");
        return 1;
    }

    struct ipc_port *port = ipc_port_alloc(receiver);
    if (!port) {
        ipc_space_unlock(r_space);
        serial_putstr("  FAIL: could not allocate port\r\n");
        return 1;
    }

    r_space->is_table[r_port_name].ie_object = port;
    r_space->is_table[r_port_name].ie_bits   = IE_BITS_RECEIVE;
    ipc_space_unlock(r_space);

    /* Grant sender a send right */
    struct ipc_space *s_space = sender->t_ipc_space;
    mach_port_name_t s_port_name;

    ipc_space_lock(s_space);
    kr = ipc_space_alloc_name(s_space, &s_port_name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(s_space);
        serial_putstr("  FAIL: could not allocate sender port name\r\n");
        return 1;
    }

    s_space->is_table[s_port_name].ie_object = port;
    s_space->is_table[s_port_name].ie_bits   = IE_BITS_SEND;
    ipc_port_lock(port);
    port->ip_send_rights++;
    ipc_port_unlock(port);
    ipc_space_unlock(s_space);

    serial_putstr("  Setup complete. Running ");
    serial_putdec(PERF_ITERATIONS);
    serial_putstr(" iterations...\r\n\r\n");

    /* --- Warm up TSC and caches --- */
    (void)rdtsc();
    (void)rdtsc();

    /*
     * All benchmarks use paired send+receive to avoid exceeding the
     * queue depth limit (IPC_MQUEUE_MAX_DEPTH = 16).  Each iteration
     * sends one message, then immediately receives it, so the queue
     * never holds more than one message at a time.
     *
     * We time only the operation of interest in each benchmark.
     */

    /* --- Benchmark 1: Send cost (time send only, untimed receive to drain) --- */
    uint64_t send_min = ~(uint64_t)0, send_max = 0, send_total = 0;
    uint32_t send_count = 0;

    for (uint32_t i = 0; i < PERF_ITERATIONS; i++) {
        struct perf_message msg;
        kmemset(&msg, 0, sizeof(msg));
        msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
        msg.header.msgh_size        = sizeof(msg);
        msg.header.msgh_remote_port = s_port_name;
        msg.header.msgh_local_port  = MACH_PORT_NULL;
        msg.header.msgh_id          = 100;
        msg.seq = i;

        uint64_t t0 = rdtsc();
        kr = mach_msg_send(sender, &msg.header, sizeof(msg));
        uint64_t t1 = rdtsc();

        if (kr != KERN_SUCCESS) break;
        send_count++;

        uint64_t dt = t1 - t0;
        send_total += dt;
        if (dt < send_min) send_min = dt;
        if (dt > send_max) send_max = dt;

        /* Drain the message so queue stays empty */
        struct perf_message drain;
        mach_msg_size_t drain_size = 0;
        mach_msg_receive(receiver, r_port_name, &drain, sizeof(drain), &drain_size);
    }

    /* --- Benchmark 2: Receive cost (untimed send to fill, time receive) --- */
    uint64_t recv_min = ~(uint64_t)0, recv_max = 0, recv_total = 0;
    uint32_t recv_count = 0;

    for (uint32_t i = 0; i < PERF_ITERATIONS; i++) {
        /* Pre-send a message (untimed) */
        struct perf_message msg;
        kmemset(&msg, 0, sizeof(msg));
        msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
        msg.header.msgh_size        = sizeof(msg);
        msg.header.msgh_remote_port = s_port_name;
        msg.header.msgh_local_port  = MACH_PORT_NULL;
        msg.header.msgh_id          = 150;
        msg.seq = i;

        kr = mach_msg_send(sender, &msg.header, sizeof(msg));
        if (kr != KERN_SUCCESS) break;

        struct perf_message buf;
        mach_msg_size_t out_size = 0;

        uint64_t t0 = rdtsc();
        kr = mach_msg_receive(receiver, r_port_name,
                              &buf, sizeof(buf), &out_size);
        uint64_t t1 = rdtsc();

        if (kr != KERN_SUCCESS) break;
        recv_count++;

        uint64_t dt = t1 - t0;
        recv_total += dt;
        if (dt < recv_min) recv_min = dt;
        if (dt > recv_max) recv_max = dt;
    }

    /* --- Benchmark 3: Round-trip (send + immediately receive, timed together) --- */
    uint64_t rt_min = ~(uint64_t)0, rt_max = 0, rt_total = 0;
    uint32_t rt_count = 0;

    for (uint32_t i = 0; i < PERF_ITERATIONS; i++) {
        struct perf_message msg;
        kmemset(&msg, 0, sizeof(msg));
        msg.header.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
        msg.header.msgh_size        = sizeof(msg);
        msg.header.msgh_remote_port = s_port_name;
        msg.header.msgh_local_port  = MACH_PORT_NULL;
        msg.header.msgh_id          = 200;
        msg.seq = i;

        struct perf_message buf;
        mach_msg_size_t out_size = 0;

        uint64_t t0 = rdtsc();
        kr = mach_msg_send(sender, &msg.header, sizeof(msg));
        if (kr == KERN_SUCCESS)
            kr = mach_msg_receive(receiver, r_port_name,
                                  &buf, sizeof(buf), &out_size);
        uint64_t t1 = rdtsc();

        if (kr != KERN_SUCCESS) break;
        rt_count++;

        uint64_t dt = t1 - t0;
        rt_total += dt;
        if (dt < rt_min) rt_min = dt;
        if (dt > rt_max) rt_max = dt;
    }

    /* --- Print results --- */
    if (send_count > 0)
        print_stats("send      ", send_min, send_max, send_total, send_count);
    if (recv_count > 0)
        print_stats("receive   ", recv_min, recv_max, recv_total, recv_count);
    if (rt_count > 0)
        print_stats("round-trip", rt_min, rt_max, rt_total, rt_count);

    serial_putstr("\r\n  Note: Phase 1 (non-blocking, no contention, kfree=no-op)\r\n");
    serial_putstr("  These are lower-bound figures.\r\n");
    serial_putstr("========================================\r\n");
    serial_putstr("[UNHOX] IPC performance baseline recorded.\r\n");

    /* Cleanup */
    task_destroy(sender);
    task_destroy(receiver);

    return 0;
}
