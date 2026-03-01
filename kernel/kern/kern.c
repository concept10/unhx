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
#include "ipc/ipc.h"
#include "vm/vm.h"

/* Simple serial output helper (platform-specific, defined in platform/) */
extern void serial_putstr(const char *s);

void kern_init(void)
{
    /*
     * TODO (Phase 2):
     *   1. Create the kernel task (task 0) with its ipc_space.
     *   2. Allocate the idle thread.
     *   3. Initialise the scheduler run queue.
     */
}

void kernel_main(void)
{
    serial_putstr("[UNHOX] kernel_main entered\r\n");

    /* Initialise subsystems in dependency order */
    ipc_init();
    vm_init(0, 0);  /* TODO: pass real Multiboot2 memory map */
    kern_init();

    serial_putstr("[UNHOX] subsystems initialised\r\n");

    /*
     * TODO (Phase 1 milestone — Prompt 3.3):
     *   Call create_test_tasks() here to exercise IPC between two kernel tasks.
     */

    /* Halt — scheduler loop goes here in Phase 2 */
    serial_putstr("[UNHOX] halting (no scheduler yet)\r\n");
    for (;;)
        __asm__ volatile ("hlt");
}
