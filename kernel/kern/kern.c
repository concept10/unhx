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
#include "task.h"
#include "sched.h"
#include "ipc/ipc.h"
#include "vm/vm.h"

#ifdef UNHOX_BOOT_TESTS
#include "tests/ipc_test.h"
#include "tests/ipc_perf.h"
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

/*
 * Scheduler test threads — print alternating output to verify that
 * the PIT timer interrupt drives preemptive context switching.
 * Each thread loops, printing its label every ~500 ms (50 iterations
 * of a busy wait calibrated to roughly 10 ms each).
 */
static volatile int sched_test_done;

static void busy_wait(void)
{
    for (volatile int i = 0; i < 500000; i++)
        ;
}

void sched_test_thread_a(void)
{
    for (int i = 0; i < 5; i++) {
        serial_putstr("[sched] thread A running\r\n");
        busy_wait();
    }
    serial_putstr("[sched] thread A done\r\n");
    sched_test_done++;
    for (;;)
        __asm__ volatile ("hlt");
}

void sched_test_thread_b(void)
{
    for (int i = 0; i < 5; i++) {
        serial_putstr("[sched] thread B running\r\n");
        busy_wait();
    }
    serial_putstr("[sched] thread B done\r\n");
    sched_test_done++;
    for (;;)
        __asm__ volatile ("hlt");
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
     *   0. platform — GDT (with TSS), PIC, IDT (must be before interrupts)
     *   1. kalloc   — kernel heap (needed by everything)
     *   2. IPC      — port/space infrastructure
     *   3. VM       — physical page allocator
     *   4. kern     — kernel task, scheduler
     */
    serial_putstr("[UNHOX] initialising platform...\r\n");
    extern void platform_init(void);
    platform_init();

    serial_putstr("[UNHOX] initialising kernel heap...\r\n");
    kalloc_init();

    serial_putstr("[UNHOX] initialising IPC subsystem...\r\n");
    ipc_init();

    serial_putstr("[UNHOX] initialising VM subsystem...\r\n");
    vm_init(0, 0);  /* TODO: parse real Multiboot2 memory map */

    serial_putstr("[UNHOX] activating zone allocator...\r\n");
    kalloc_zones_init();

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
     * IPC performance baseline:
     * Measures send/receive/round-trip costs in TSC cycles.
     */
    ipc_perf_run();
#endif

    /*
     * Preemptive scheduling test:
     * Create two threads that print alternating output, driven by
     * the PIT timer interrupt (no explicit yield).
     */
    serial_putstr("\r\n[UNHOX] creating scheduler test threads...\r\n");
    {
        struct task *ktask = kernel_task_ptr();

        /*
         * Create a "boot" thread representing the current execution context.
         * This becomes the idle thread once sched_run() enters the halt loop.
         */
        struct thread *boot_th = thread_create(ktask, (void *)0, 0);
        if (boot_th)
            sched_set_current(boot_th);

        /* Thread A: prints "A" periodically */
        struct thread *th_a = thread_create(ktask, sched_test_thread_a, 0);
        if (th_a)
            sched_enqueue(th_a);

        /* Thread B: prints "B" periodically */
        struct thread *th_b = thread_create(ktask, sched_test_thread_b, 0);
        if (th_b)
            sched_enqueue(th_b);

        serial_putstr("[UNHOX] threads created, entering scheduler\r\n");
    }

    /* Enable interrupts and enter the scheduler — never returns */
    sched_run();
}
