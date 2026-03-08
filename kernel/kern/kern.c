/*
 * kernel/kern/kern.c — Kernel core initialisation and kernel_main for UNHOX
 *
 * kernel_main() is the C entry point invoked by the assembly boot stub
 * (kernel/platform/boot.S) after the GDT is loaded, the initial stack is
 * set up, and the BSS section has been zeroed.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §4 — Tasks and Threads.
 */

#include "kern.h"
#include "kalloc.h"
#include "kernel_task.h"
#include "sched.h"
#include "ipc/ipc.h"
#include "vm/vm.h"

#ifdef UNHOX_BOOT_TESTS
#include "tests/ipc_test.h"
#include "tests/ipc/ipc_roundtrip_test.h"
#include "tests/ipc/ipc_perf.h"
#endif

/* Serial output (platform layer) */
extern void serial_putstr(const char *s);

/* Bootstrap server entry (Phase 1: kernel-internal) */
extern void bootstrap_main(void);

void kern_init(void)
{
    /* Create the kernel task (task 0) with its ipc_space */
    kernel_task_init();

    /* Initialise the scheduler */
    sched_init();
}

void kernel_main(void)
{
    serial_putstr("\r\n");
    serial_putstr("================================================\r\n");
    serial_putstr("  UNHOX — U Is Not Hurd Or X\r\n");
    serial_putstr("  Mach microkernel — Phase 1\r\n");
    serial_putstr("================================================\r\n");
    serial_putstr("[UNHOX] kernel_main entered\r\n");

    /*
     * Initialise subsystems in dependency order:
     *   1. kalloc  — kernel heap (needed by everything)
     *   2. IPC     — port/space infrastructure
     *   3. VM      — physical page allocator
     *   4. kern    — kernel task, scheduler
     */
    serial_putstr("[UNHOX] initialising kernel heap...\r\n");
    kalloc_init();

    serial_putstr("[UNHOX] initialising IPC subsystem...\r\n");
    ipc_init();

    serial_putstr("[UNHOX] initialising VM subsystem...\r\n");
    vm_init(0, 0);  /* TODO: parse real Multiboot2 memory map */

    serial_putstr("[UNHOX] initialising kernel core...\r\n");
    kern_init();

    serial_putstr("[UNHOX] all subsystems initialised\r\n");

    /*
     * Phase 1 IPC smoke test (Prompt 3.3):
     * Create two tasks and pass a Mach message between them.
     */
    serial_putstr("\r\n");
    create_test_tasks();

    /*
     * Bootstrap server (Phase 1: kernel-internal demo)
     */
    serial_putstr("\r\n");
    bootstrap_main();

#ifdef UNHOX_BOOT_TESTS
    /*
     * Formal IPC milestone test (Prompt 8 — v0.2):
     * Comprehensive test suite with PASS/FAIL reporting.
     */
    serial_putstr("\r\n");
    int test_result = ipc_test_run();

    if (test_result == 0) {
        serial_putstr("\r\n[UNHOX] All milestone tests PASSED.\r\n");
    } else {
        serial_putstr("\r\n[UNHOX] Milestone tests FAILED.\r\n");
    }

    /*
     * IPC round-trip correctness test:
     * Exercises ipc_right.c and mach_msg_trap() with 5 test scenarios.
     */
    serial_putstr("\r\n");
    ipc_roundtrip_test_run();

    /*
     * IPC performance benchmark:
     * Measures null Mach message round-trip latency via TSC.
     */
    serial_putstr("\r\n");
    ipc_perf_run();
#endif

    /* Halt — preemptive scheduler loop goes here in Phase 2 */
    serial_putstr("\r\n[UNHOX] halting (cooperative scheduling only in Phase 1)\r\n");
    for (;;)
        __asm__ volatile ("hlt");
}
