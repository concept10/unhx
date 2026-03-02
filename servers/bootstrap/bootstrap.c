/*
 * servers/bootstrap/bootstrap.c — Bootstrap server main entry for UNHOX
 *
 * Phase 2 implementation:
 *   - Runs as a dedicated kernel thread (not a direct call from kernel_main)
 *   - Allocates a real ipc_port as the bootstrap port (receive right)
 *   - Sits in a blocking ipc_mqueue_receive() loop
 *   - Dispatches REGISTER and LOOKUP requests by msgh_id
 *   - Replies to LOOKUP via the reply_port field in the request body
 *
 * The global `bootstrap_port` is set before the message loop starts so that
 * any thread created after the bootstrap thread can send messages to it.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap.c for the original implementation.
 */

#include "bootstrap.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/klib.h"
#include "kern/task.h"

/* Serial output (from kernel platform layer) */
extern void serial_putstr(const char *s);

/* -------------------------------------------------------------------------
 * Global bootstrap port
 * Set once by bootstrap_main() before entering the message loop.
 * Other kernel threads read this to send messages to bootstrap.
 * ------------------------------------------------------------------------- */
struct ipc_port *bootstrap_port = (void *)0;

/* -------------------------------------------------------------------------
 * bootstrap_main — bootstrap server thread entry point
 * ------------------------------------------------------------------------- */

void bootstrap_main(void)
{
    serial_putstr("[bootstrap] initialising bootstrap server\r\n");
    bootstrap_init();

    /* Allocate the bootstrap receive port */
    bootstrap_port = ipc_port_alloc(kernel_task_ptr());
    if (!bootstrap_port) {
        serial_putstr("[bootstrap] FATAL: failed to allocate bootstrap port\r\n");
        for (;;)
            __asm__ volatile ("hlt");
    }

    serial_putstr("[bootstrap] bootstrap server ready\r\n");

    /*
     * Message receive loop.
     *
     * We block on ipc_mqueue_receive() until a client sends us a message.
     * The preemptive scheduler will run us when a message arrives (via
     * sched_wakeup() called from ipc_mqueue_send()).
     *
     * Buffer sized for the largest request (bootstrap_lookup_msg_t).
     */
    uint8_t buf[sizeof(bootstrap_lookup_msg_t) + 16];

    for (;;) {
        mach_msg_size_t msg_size = 0;
        mach_msg_return_t mr = ipc_mqueue_receive(
            bootstrap_port->ip_messages,
            buf, sizeof(buf),
            &msg_size,
            1 /* blocking */);

        if (mr != MACH_MSG_SUCCESS)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

        switch (hdr->msgh_id) {

        case BOOTSTRAP_MSG_REGISTER: {
            if (msg_size < sizeof(bootstrap_register_msg_t))
                break;
            bootstrap_register_msg_t *req = (bootstrap_register_msg_t *)buf;

            /* Ensure name is null-terminated */
            req->name[BOOTSTRAP_NAME_MAX - 1] = '\0';

            int r = bootstrap_register(req->name, req->service_port);
            if (r == BOOTSTRAP_SUCCESS) {
                serial_putstr("[bootstrap] REGISTER: ");
                serial_putstr(req->name);
                serial_putstr("\r\n");
            } else if (r == BOOTSTRAP_NAME_IN_USE) {
                serial_putstr("[bootstrap] REGISTER (already registered): ");
                serial_putstr(req->name);
                serial_putstr("\r\n");
            } else {
                serial_putstr("[bootstrap] REGISTER failed: ");
                serial_putstr(req->name);
                serial_putstr("\r\n");
            }
            break;
        }

        case BOOTSTRAP_MSG_LOOKUP: {
            if (msg_size < sizeof(bootstrap_lookup_msg_t))
                break;
            bootstrap_lookup_msg_t *req = (bootstrap_lookup_msg_t *)buf;

            req->name[BOOTSTRAP_NAME_MAX - 1] = '\0';

            uint64_t found_port = 0;
            int r = bootstrap_lookup(req->name, &found_port);

            serial_putstr("[bootstrap] LOOKUP: ");
            serial_putstr(req->name);
            serial_putstr(r == BOOTSTRAP_SUCCESS ? " -> found\r\n"
                                                 : " -> not found\r\n");

            /* Send reply if the client provided a reply port */
            if (req->reply_port) {
                bootstrap_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = BOOTSTRAP_MSG_REPLY;
                reply.retcode       = r;
                reply.port_val      = found_port;

                struct ipc_port *rport = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rport->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        default:
            serial_putstr("[bootstrap] unknown message id\r\n");
            break;
        }
    }
}
