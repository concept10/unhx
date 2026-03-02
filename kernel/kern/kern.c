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
#include "thread.h"
#include "sched.h"
#include "ipc/ipc.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_space.h"
#include "ipc/ipc_mqueue.h"
#include "vm/vm.h"
#include "vm/vm_map.h"
#include "klib.h"
#include "multiboot.h"
#include "elf_load.h"
#include "platform/paging.h"

#ifdef UNHOX_BOOT_TESTS
#include "tests/ipc_test.h"
#include "tests/ipc_perf.h"
#endif

/* Serial output (platform layer) */
extern void serial_putstr(const char *s);

/* Bootstrap server (Phase 2: real Mach message loop as a kernel thread) */
#include "bootstrap/bootstrap.h"

void kern_init(void)
{
    /* Create the kernel task (task 0) with its ipc_space */
    kernel_task_init();

    /* Initialise the scheduler */
    sched_init();
}

/* -------------------------------------------------------------------------
 * Blocking IPC test — thread A blocks on receive, thread B sends.
 * Verifies that blocking IPC works: A sleeps until B's message arrives.
 * ------------------------------------------------------------------------- */

static struct ipc_port *blocking_test_port;

static void blocking_ipc_receiver(void)
{
    serial_putstr("[ipc-block] receiver: blocking on receive...\r\n");

    /* Block until a message arrives */
    uint8_t buf[256];
    mach_msg_size_t out_size = 0;
    mach_msg_return_t mr = ipc_mqueue_receive(blocking_test_port->ip_messages,
                                               buf, sizeof(buf), &out_size,
                                               1 /* blocking */);

    if (mr == MACH_MSG_SUCCESS) {
        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
        const char *payload = (const char *)(buf + sizeof(mach_msg_header_t));
        (void)hdr;
        serial_putstr("[ipc-block] receiver: got message: ");
        serial_putstr(payload);
        serial_putstr("\r\n");
        serial_putstr("[ipc-block] PASS — blocking IPC works\r\n");
    } else {
        serial_putstr("[ipc-block] FAIL — receive returned error\r\n");
    }

    for (;;)
        __asm__ volatile ("hlt");
}

static void blocking_ipc_sender(void)
{
    /* Delay to ensure receiver blocks first */
    for (volatile int i = 0; i < 2000000; i++)
        ;

    serial_putstr("[ipc-block] sender: sending message...\r\n");

    /* Build a message */
    uint8_t buf[256];
    kmemset(buf, 0, sizeof(buf));
    mach_msg_header_t *hdr = (mach_msg_header_t *)buf;
    hdr->msgh_size = sizeof(mach_msg_header_t) + 16;
    const char *msg = "hello_blocking";
    kmemcpy(buf + sizeof(mach_msg_header_t), msg, 15);

    mach_msg_return_t mr = ipc_mqueue_send(blocking_test_port->ip_messages,
                                            buf, hdr->msgh_size);

    if (mr == MACH_MSG_SUCCESS) {
        serial_putstr("[ipc-block] sender: message sent\r\n");
    } else {
        serial_putstr("[ipc-block] sender: send FAILED\r\n");
    }

    for (;;)
        __asm__ volatile ("hlt");
}

/* -------------------------------------------------------------------------
 * Scheduler test threads — print alternating output to verify that
 * the PIT timer interrupt drives preemptive context switching.
 * ------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------
 * Bootstrap IPC test thread — exercises the real Mach message-based API.
 *
 * Sends BOOTSTRAP_MSG_REGISTER then BOOTSTRAP_MSG_LOOKUP to the bootstrap
 * server thread via ipc_mqueue_send(), blocks on a reply port, and verifies
 * the returned port matches what was registered.
 * ------------------------------------------------------------------------- */
static void bootstrap_ipc_test(void)
{
    /* Spin until the bootstrap thread has set bootstrap_port */
    while (!bootstrap_port)
        for (volatile int i = 0; i < 50000; i++)
            ;

    struct task *ktask = kernel_task_ptr();

    /* Allocate a fake "service" port to register */
    struct ipc_port *svc_port = ipc_port_alloc(ktask);
    if (!svc_port) {
        serial_putstr("[bs-test] FAIL — could not allocate service port\r\n");
        goto done;
    }

    /* ---- REGISTER ---- */
    bootstrap_register_msg_t reg;
    kmemset(&reg, 0, sizeof(reg));
    reg.hdr.msgh_size = sizeof(reg);
    reg.hdr.msgh_id   = BOOTSTRAP_MSG_REGISTER;
    kstrncpy(reg.name, "com.unhox.test_svc", BOOTSTRAP_NAME_MAX);
    reg.service_port  = (uint64_t)svc_port;

    ipc_mqueue_send(bootstrap_port->ip_messages, &reg, sizeof(reg));

    /* ---- LOOKUP ---- */
    /* Both REGISTER and LOOKUP can be queued together — the FIFO queue
     * guarantees REGISTER is dequeued and processed before LOOKUP. */
    struct ipc_port *reply_port = ipc_port_alloc(ktask);
    if (!reply_port) {
        serial_putstr("[bs-test] FAIL — could not allocate reply port\r\n");
        goto done;
    }

    bootstrap_lookup_msg_t lkup;
    kmemset(&lkup, 0, sizeof(lkup));
    lkup.hdr.msgh_size = sizeof(lkup);
    lkup.hdr.msgh_id   = BOOTSTRAP_MSG_LOOKUP;
    kstrncpy(lkup.name, "com.unhox.test_svc", BOOTSTRAP_NAME_MAX);
    lkup.reply_port    = (uint64_t)reply_port;

    ipc_mqueue_send(bootstrap_port->ip_messages, &lkup, sizeof(lkup));

    /* Block on the reply */
    uint8_t rbuf[sizeof(bootstrap_reply_msg_t)];
    mach_msg_size_t out_size = 0;
    mach_msg_return_t mr = ipc_mqueue_receive(
        reply_port->ip_messages, rbuf, sizeof(rbuf), &out_size,
        1 /* blocking */);

    if (mr == MACH_MSG_SUCCESS) {
        bootstrap_reply_msg_t *reply = (bootstrap_reply_msg_t *)rbuf;
        if (reply->retcode == BOOTSTRAP_SUCCESS &&
            reply->port_val == (uint64_t)svc_port) {
            serial_putstr("[bs-test] PASS — bootstrap IPC register+lookup works\r\n");
        } else {
            serial_putstr("[bs-test] FAIL — reply mismatch\r\n");
        }
    } else {
        serial_putstr("[bs-test] FAIL — receive error\r\n");
    }

done:
    for (;;)
        __asm__ volatile ("hlt");
}

void kernel_main(uint32_t mb_info_phys)
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

    serial_putstr("[UNHOX] initialising paging...\r\n");
    paging_init(0, 0);

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
     * Bootstrap server (Phase 2: blocking Mach message loop as kernel thread).
     * We create the thread now; it will run once the scheduler starts and will
     * block on ipc_mqueue_receive() waiting for register/lookup requests.
     */

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
     * Set up scheduling: create a boot thread to represent kernel_main,
     * then launch test threads.
     */
    serial_putstr("\r\n[UNHOX] creating test threads...\r\n");
    {
        struct task *ktask = kernel_task_ptr();

        /*
         * Create a "boot" thread representing the current execution context.
         * This becomes the idle thread once sched_run() enters the halt loop.
         */
        struct thread *boot_th = thread_create(ktask, (void *)0, 0);
        if (boot_th)
            sched_set_current(boot_th);

        /*
         * Bootstrap server thread — enqueued first so it initialises and
         * sets bootstrap_port before the other test threads need it.
         * It immediately blocks on ipc_mqueue_receive() once ready.
         */
        struct thread *th_bs = thread_create(ktask, bootstrap_main, 0);
        if (th_bs)
            sched_enqueue(th_bs);

        /*
         * Bootstrap IPC test thread — runs after bootstrap_main has had a
         * chance to set bootstrap_port and enter its receive loop.
         */
        struct thread *th_bst = thread_create(ktask, bootstrap_ipc_test, 0);
        if (th_bst)
            sched_enqueue(th_bst);

        /*
         * Blocking IPC test:
         * Thread "receiver" blocks on receive, thread "sender" sends after delay.
         */
        blocking_test_port = ipc_port_alloc(ktask);
        if (blocking_test_port) {
            struct thread *th_recv = thread_create(ktask, blocking_ipc_receiver, 0);
            struct thread *th_send = thread_create(ktask, blocking_ipc_sender, 0);
            if (th_recv) sched_enqueue(th_recv);
            if (th_send) sched_enqueue(th_send);
        }

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

    /*
     * Launch the init userspace process (Phase 2 — milestone v0.4).
     *
     * QEMU passes init.elf as a Multiboot1 module via -initrd.
     * We parse the module list, load the ELF into a new user task's
     * address space, and create a ring-3 thread that enters via iretq.
     */
    serial_putstr("\r\n[UNHOX] loading userspace init...\r\n");
    {
        struct multiboot_info *mbinfo =
            (struct multiboot_info *)(uint64_t)mb_info_phys;

        if (mbinfo && (mbinfo->flags & MULTIBOOT_INFO_MODS) &&
            mbinfo->mods_count > 0) {

            struct multiboot_mod *mods =
                (struct multiboot_mod *)(uint64_t)mbinfo->mods_addr;

            const void *elf_data = (const void *)(uint64_t)mods[0].mod_start;
            uint64_t    elf_size = mods[0].mod_end - mods[0].mod_start;

            serial_putstr("[UNHOX] found init module, creating user task\r\n");

            /* Create a new task and give it a per-task PML4 */
            struct task *utask = task_create(kernel_task_ptr());
            if (utask) {
                uint64_t new_pml4 = paging_create_task_pml4();
                if (new_pml4) {
                    utask->t_cr3      = new_pml4;
                    utask->t_map->pml4 = (uint64_t *)new_pml4;
                }

                /* Load ELF segments into the task's address space */
                uint64_t entry = 0;
                kern_return_t kr = elf_load(utask, elf_data, elf_size, &entry);
                if (kr == KERN_SUCCESS) {
                    serial_putstr("[UNHOX] ELF loaded, setting up user stack\r\n");

                    /* Map a 64 KB user stack at 0x7FFF0000 */
                    uint64_t ustack_base = 0x7FFF0000ULL;
                    uint64_t ustack_size = 0x10000ULL;   /* 64 KB */
                    vm_map_enter(utask->t_map, ustack_base, ustack_size,
                                 VM_PROT_READ | VM_PROT_WRITE);

                    /*
                     * RSP at entry: top-of-stack minus 16 bytes so that
                     * _start's `call main` leaves RSP 16-byte aligned at
                     * main's function entry (System V AMD64 ABI).
                     */
                    uint64_t user_rsp = (ustack_base + ustack_size) - 16;

                    struct thread *uth =
                        thread_create_user(utask, entry, user_rsp);
                    if (uth) {
                        sched_enqueue(uth);
                        serial_putstr("[UNHOX] init task queued — will run in ring 3\r\n");
                    } else {
                        serial_putstr("[UNHOX] WARN: thread_create_user failed\r\n");
                    }
                } else {
                    serial_putstr("[UNHOX] WARN: elf_load failed\r\n");
                }
            } else {
                serial_putstr("[UNHOX] WARN: task_create for init failed\r\n");
            }
        } else {
            serial_putstr("[UNHOX] no Multiboot module found — skipping userspace\r\n");
        }
    }

    /* Enable interrupts and enter the scheduler — never returns */
    sched_run();
}
