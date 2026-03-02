/*
 * servers/bootstrap/bootstrap.c — Bootstrap server main entry for UNHU
 *
 * The bootstrap server is the first userspace task.  In the full Mach
 * design it runs as a separate process with its own address space, receiving
 * registration and lookup requests as Mach messages on its bootstrap port.
 *
 * Phase 1 implementation:
 *   - Runs as a kernel-internal function (not a separate task yet)
 *   - Uses the registry (registry.c) as a direct C API
 *   - Demonstrates the registration and lookup flow
 *
 * TODO (Phase 2): Make this a real userspace server that:
 *   1. Holds the receive right to the bootstrap port
 *   2. Sits in a mach_msg() loop receiving bootstrap_register and
 *      bootstrap_lookup messages
 *   3. Replies with port send rights to the requesting client
 *   4. Is loaded by the kernel as the first user task from a Multiboot
 *      module or embedded initrd
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap.c for the original implementation.
 */

#include "bootstrap.h"

/* Serial output (from kernel platform layer — temporary for Phase 1) */
extern void serial_putstr(const char *s);

/*
 * bootstrap_main — entry point for the bootstrap server.
 *
 * For Phase 1, this initialises the registry and runs a basic self-test
 * to verify the registration and lookup mechanism works.
 */
void bootstrap_main(void)
{
    serial_putstr("[bootstrap] initialising bootstrap server\r\n");
    bootstrap_init();

    /* Register some placeholder services to demonstrate the mechanism */
    int r;

    r = bootstrap_register("com.unhu.kernel", 1);
    if (r == BOOTSTRAP_SUCCESS)
        serial_putstr("[bootstrap] registered: com.unhu.kernel\r\n");

    r = bootstrap_register("com.unhu.ipc_test", 2);
    if (r == BOOTSTRAP_SUCCESS)
        serial_putstr("[bootstrap] registered: com.unhu.ipc_test\r\n");

    /* Verify lookup works */
    uint32_t port = 0;
    r = bootstrap_lookup("com.unhu.kernel", &port);
    if (r == BOOTSTRAP_SUCCESS && port == 1)
        serial_putstr("[bootstrap] lookup com.unhu.kernel → port 1 (OK)\r\n");

    r = bootstrap_lookup("com.unhu.nonexistent", &port);
    if (r == BOOTSTRAP_UNKNOWN_SERVICE)
        serial_putstr("[bootstrap] lookup com.unhu.nonexistent → not found (OK)\r\n");

    /* Verify duplicate registration is rejected */
    r = bootstrap_register("com.unhu.kernel", 99);
    if (r == BOOTSTRAP_NAME_IN_USE)
        serial_putstr("[bootstrap] duplicate registration rejected (OK)\r\n");

    serial_putstr("[bootstrap] bootstrap server ready\r\n");

    /*
     * In Phase 2, we would enter a message loop here:
     *
     *   for (;;) {
     *       mach_msg_header_t msg;
     *       kr = mach_msg(&msg, MACH_RCV_MSG, ...);
     *       switch (msg.msgh_id) {
     *           case BOOTSTRAP_REGISTER: ...
     *           case BOOTSTRAP_LOOKUP:   ...
     *           case BOOTSTRAP_CHECKIN:  ...
     *       }
     *   }
     */
}
