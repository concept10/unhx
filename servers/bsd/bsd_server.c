/*
 * servers/bsd/bsd_server.c — Minimal BSD server for UNHOX
 *
 * Runs as a kernel thread. Maintains a simple file descriptor table where
 * fd 0/1/2 map to the serial console. Registers as "com.unhox.bsd" with
 * the bootstrap server and serves BSD_MSG_WRITE / BSD_MSG_READ requests.
 *
 * Reference: OSF MK servers/bsd/bsd_server.c
 */

#include "bsd/bsd_msg.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/klib.h"
#include "kern/task.h"
#include "bootstrap/bootstrap.h"

extern void serial_putstr(const char *s);
extern void serial_putchar(char c);
extern char serial_getchar(void);

/* -------------------------------------------------------------------------
 * Global BSD port
 * ------------------------------------------------------------------------- */

struct ipc_port *bsd_port = (void *)0;

/* -------------------------------------------------------------------------
 * bsd_server_main — BSD server thread entry point
 * ------------------------------------------------------------------------- */

void bsd_server_main(void)
{
    serial_putstr("[bsd] initialising BSD server\r\n");

    bsd_port = ipc_port_alloc(kernel_task_ptr());
    if (!bsd_port) {
        serial_putstr("[bsd] FATAL: could not allocate BSD port\r\n");
        for (;;)
            __asm__ volatile ("hlt");
    }

    /* Wait for the bootstrap server to be ready */
    while (!bootstrap_port)
        for (volatile int i = 0; i < 10000; i++)
            ;

    /* Register as "com.unhox.bsd" */
    bootstrap_register_msg_t reg;
    kmemset(&reg, 0, sizeof(reg));
    reg.hdr.msgh_size = sizeof(reg);
    reg.hdr.msgh_id   = BOOTSTRAP_MSG_REGISTER;
    kstrncpy(reg.name, "com.unhox.bsd", BOOTSTRAP_NAME_MAX);
    reg.service_port  = (uint64_t)bsd_port;
    ipc_mqueue_send(bootstrap_port->ip_messages, &reg, sizeof(reg));

    serial_putstr("[bsd] BSD server ready\r\n");

    /*
     * Message receive loop.
     * Buffer sized for the largest request (bsd_write_msg_t).
     */
    uint8_t buf[sizeof(bsd_write_msg_t) + 16];

    for (;;) {
        mach_msg_size_t msg_size = 0;
        mach_msg_return_t mr = ipc_mqueue_receive(
            bsd_port->ip_messages,
            buf, sizeof(buf),
            &msg_size,
            1 /* blocking */);

        if (mr != MACH_MSG_SUCCESS)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

        switch (hdr->msgh_id) {

        case BSD_MSG_WRITE: {
            if (msg_size < sizeof(mach_msg_header_t) + 2 * sizeof(uint32_t))
                break;
            bsd_write_msg_t *req = (bsd_write_msg_t *)buf;

            /* fd 1 and fd 2 → serial output */
            if (req->fd == 1 || req->fd == 2) {
                uint32_t n = req->count < BSD_DATA_MAX ? req->count : BSD_DATA_MAX;
                for (uint32_t i = 0; i < n; i++)
                    serial_putchar((char)req->data[i]);
            }
            break;
        }

        case BSD_MSG_READ: {
            if (msg_size < sizeof(bsd_read_msg_t))
                break;
            bsd_read_msg_t *req = (bsd_read_msg_t *)buf;

            bsd_reply_msg_t reply;
            kmemset(&reply, 0, sizeof(reply));
            reply.hdr.msgh_size = sizeof(reply);
            reply.hdr.msgh_id   = BSD_MSG_REPLY;

            /* fd 0 → serial input (non-blocking) */
            if (req->fd == 0) {
                char c = serial_getchar();
                if (c != 0) {
                    reply.data[0] = (uint8_t)c;
                    reply.count   = 1;
                } else {
                    reply.count   = 0;
                }
                reply.retcode = BSD_SUCCESS;
            } else {
                reply.retcode = BSD_BAD_FD;
            }

            if (req->reply_port) {
                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        default:
            break;
        }
    }
}
