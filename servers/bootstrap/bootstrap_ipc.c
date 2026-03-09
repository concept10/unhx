/*
 * servers/bootstrap/bootstrap_ipc.c — IPC-based bootstrap server for NEOMACH
 *
 * Phase 2 bootstrap server implementation using real Mach IPC messages.
 * The server holds a receive right on the global bootstrap port and
 * processes BOOTSTRAP_IPC_MSG_REGISTER and BOOTSTRAP_IPC_MSG_LOOKUP
 * messages synchronously (via bootstrap_ipc_run_once()).
 *
 * Architecture:
 *
 *   bootstrap_server_task (kernel task)
 *       ↓  receive right
 *   bootstrap_port           ← client tasks send to this port
 *       ↑ send right         (given to all tasks on creation — future work)
 *
 *   Client calls bootstrap_ipc_register():
 *     1. Builds a BOOTSTRAP_IPC_MSG_REGISTER message with COPY_SEND descriptor
 *     2. Sends to bootstrap_port
 *     3. Calls bootstrap_ipc_run_once() to process synchronously
 *
 *   Client calls bootstrap_ipc_lookup():
 *     1. Allocates a reply port in caller's space
 *     2. Sends BOOTSTRAP_IPC_MSG_LOOKUP (with reply port) to bootstrap_port
 *     3. Calls bootstrap_ipc_run_once() to let server process
 *     4. Receives reply from server (carrying service COPY_SEND right)
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap.c for message protocol conventions.
 */

#include "bootstrap_ipc.h"
#include "bootstrap.h"
#include "kern/task.h"
#include "kern/klib.h"
#include "ipc/ipc.h"
#include "ipc/ipc_kmsg.h"
#include "ipc/ipc_right.h"
#include "mach/mach_types.h"

/* Serial output (kernel platform layer) */
extern void serial_putstr(const char *s);

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

/* The bootstrap server's kernel task */
static struct task *bootstrap_server_task = (void *)0;

/* Port names in bootstrap_server_task's space */
static mach_port_name_t bstrap_rcv_name = MACH_PORT_NULL;

/* The actual kernel port pointer (for giving send rights to other tasks) */
static struct ipc_port *bstrap_port = (void *)0;

/* -------------------------------------------------------------------------
 * bootstrap_ipc_init
 * ------------------------------------------------------------------------- */

void bootstrap_ipc_init(void)
{
    /* Create the bootstrap server task */
    bootstrap_server_task = task_create(kernel_task_ptr());
    if (!bootstrap_server_task) {
        serial_putstr("[bootstrap_ipc] FATAL: failed to create server task\r\n");
        return;
    }

    /* Allocate the bootstrap port in the server task's space */
    kern_return_t kr = ipc_right_alloc_receive(bootstrap_server_task,
                                                &bstrap_rcv_name,
                                                &bstrap_port,
                                                0 /* no_also_send */);
    if (kr != KERN_SUCCESS) {
        serial_putstr("[bootstrap_ipc] FATAL: failed to allocate bootstrap port\r\n");
        return;
    }

    serial_putstr("[bootstrap_ipc] bootstrap port allocated\r\n");

    /* Also initialise the legacy C-API registry so both APIs coexist */
    bootstrap_init();
}

struct ipc_port *bootstrap_ipc_port(void)
{
    return bstrap_port;
}

mach_port_name_t bootstrap_ipc_port_name(void)
{
    return bstrap_rcv_name;
}

/* -------------------------------------------------------------------------
 * Process one bootstrap message (internal)
 *
 * Returns 1 if a message was processed, 0 if the queue was empty.
 * ------------------------------------------------------------------------- */

int bootstrap_ipc_run_once(void)
{
    if (!bootstrap_server_task || bstrap_rcv_name == MACH_PORT_NULL)
        return 0;

    /*
     * Use a buffer large enough for any bootstrap request.
     * The largest is bootstrap_ipc_register_request_t.
     */
    uint8_t buf[sizeof(bootstrap_ipc_register_request_t) + 8];
    mach_msg_size_t rsz = 0;
    kmemset(buf, 0, sizeof(buf));

    kern_return_t kr = mach_msg_receive(bootstrap_server_task,
                                         bstrap_rcv_name,
                                         buf, sizeof(buf), &rsz);
    if (kr != KERN_SUCCESS)
        return 0;

    mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

    /* ------------------------------------------------------------------ */
    /* BOOTSTRAP_IPC_MSG_REGISTER                                          */
    /* ------------------------------------------------------------------ */
    if (hdr->msgh_id == BOOTSTRAP_IPC_MSG_REGISTER) {
        bootstrap_ipc_register_request_t *req =
            (bootstrap_ipc_register_request_t *)buf;

        if (rsz < sizeof(*req)) {
            serial_putstr("[bootstrap_ipc] REGISTER: short message\r\n");
            return 1;
        }

        /* The service port was carried as a COPY_SEND descriptor.
         * After process_recv_descriptors ran, req->svc_port_desc.name
         * contains the port name in bootstrap_server_task's space.  */
        mach_port_name_t svc_name = req->svc_port_desc.name;

        /* Find the kernel port pointer through the server's space */
        struct ipc_space *sspace = bootstrap_server_task->t_ipc_space;
        struct ipc_port  *svc_port = (void *)0;

        ipc_space_lock(sspace);
        struct ipc_entry *e = ipc_space_lookup(sspace, svc_name);
        if (e) svc_port = e->ie_object;
        ipc_space_unlock(sspace);

        if (!svc_port) {
            serial_putstr("[bootstrap_ipc] REGISTER: port lookup failed\r\n");
            return 1;
        }

        /* Ensure name is null-terminated */
        req->name[BOOTSTRAP_IPC_NAME_MAX - 1] = '\0';

        /* Store in the legacy registry (reuse for simplicity) */
        bootstrap_register(req->name, svc_name);

        serial_putstr("[bootstrap_ipc] registered: ");
        serial_putstr(req->name);
        serial_putstr("\r\n");
        return 1;
    }

    /* ------------------------------------------------------------------ */
    /* BOOTSTRAP_IPC_MSG_LOOKUP                                            */
    /* ------------------------------------------------------------------ */
    if (hdr->msgh_id == BOOTSTRAP_IPC_MSG_LOOKUP) {
        bootstrap_ipc_lookup_request_t *req =
            (bootstrap_ipc_lookup_request_t *)buf;

        if (rsz < sizeof(*req)) {
            serial_putstr("[bootstrap_ipc] LOOKUP: short message\r\n");
            return 1;
        }

        req->name[BOOTSTRAP_IPC_NAME_MAX - 1] = '\0';

        /* Find the reply port in our space */
        mach_port_name_t reply_port = hdr->msgh_local_port;

        /* Look up in the registry */
        uint32_t svc_slot = 0;
        int result = bootstrap_lookup(req->name, &svc_slot);

        if (result != 0 /* BOOTSTRAP_SUCCESS */ || reply_port == MACH_PORT_NULL) {
            /* Send error reply if there is a reply port */
            if (reply_port != MACH_PORT_NULL) {
                bootstrap_ipc_error_reply_t err;
                kmemset(&err, 0, sizeof(err));
                err.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
                err.hdr.msgh_size        = sizeof(err);
                err.hdr.msgh_remote_port = reply_port;
                err.hdr.msgh_id          = BOOTSTRAP_IPC_MSG_REPLY_ERR;
                err.result               = result;
                mach_msg_send(bootstrap_server_task, &err.hdr, sizeof(err));
            }
            serial_putstr("[bootstrap_ipc] LOOKUP not found: ");
            serial_putstr(req->name);
            serial_putstr("\r\n");
            return 1;
        }

        /* svc_slot is the port name in the server's space. Build reply
         * with COPY_SEND of the service port to the client's reply port. */
        bootstrap_ipc_lookup_reply_t reply;
        kmemset(&reply, 0, sizeof(reply));
        reply.hdr.msgh_bits         = MACH_MSGH_BITS_COMPLEX |
                                       MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
        reply.hdr.msgh_size         = sizeof(reply);
        reply.hdr.msgh_remote_port  = reply_port;
        reply.hdr.msgh_id           = BOOTSTRAP_IPC_MSG_REPLY_OK;
        reply.body.msgh_descriptor_count = 1;
        reply.svc_port_desc.type         = MACH_MSG_PORT_DESCRIPTOR;
        reply.svc_port_desc.name         = (mach_port_t)svc_slot;
        reply.svc_port_desc.disposition  = MACH_MSG_TYPE_COPY_SEND;
        reply.result = 0;

        kern_return_t rkr = mach_msg_send(bootstrap_server_task,
                                            &reply.hdr, sizeof(reply));
        if (rkr != KERN_SUCCESS)
            serial_putstr("[bootstrap_ipc] LOOKUP: failed to send reply\r\n");
        else {
            serial_putstr("[bootstrap_ipc] LOOKUP ok: ");
            serial_putstr(req->name);
            serial_putstr("\r\n");
        }
        return 1;
    }

    /* Unknown message ID */
    serial_putstr("[bootstrap_ipc] unknown message id\r\n");
    return 1;
}

/* -------------------------------------------------------------------------
 * bootstrap_ipc_register — register a service via IPC
 * ------------------------------------------------------------------------- */

kern_return_t bootstrap_ipc_register(struct task *sender,
                                      mach_port_name_t svc_port_name,
                                      const char *name)
{
    if (!sender || !name || !bstrap_port)
        return KERN_INVALID_ARGUMENT;

    /* The sender needs a send right to the bootstrap port */
    mach_port_name_t bstrap_snd;
    kern_return_t kr = ipc_right_copy_send(bootstrap_server_task,
                                            bstrap_rcv_name,
                                            sender,
                                            &bstrap_snd);
    if (kr != KERN_SUCCESS)
        return kr;

    /* Build the REGISTER request */
    bootstrap_ipc_register_request_t req;
    kmemset(&req, 0, sizeof(req));
    req.hdr.msgh_bits         = MACH_MSGH_BITS_COMPLEX |
                                  MACH_MSGH_BITS(MACH_PORT_RIGHT_SEND, 0);
    req.hdr.msgh_size         = sizeof(req);
    req.hdr.msgh_remote_port  = bstrap_snd;
    req.hdr.msgh_id           = BOOTSTRAP_IPC_MSG_REGISTER;
    req.body.msgh_descriptor_count = 1;
    req.svc_port_desc.type         = MACH_MSG_PORT_DESCRIPTOR;
    req.svc_port_desc.name         = svc_port_name;
    req.svc_port_desc.disposition  = MACH_MSG_TYPE_COPY_SEND;
    kstrncpy(req.name, name, BOOTSTRAP_IPC_NAME_MAX);

    kr = mach_msg_send(sender, &req.hdr, sizeof(req));
    if (kr != KERN_SUCCESS)
        return kr;

    /* Process the message synchronously */
    bootstrap_ipc_run_once();

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * bootstrap_ipc_lookup — look up a service via IPC
 * ------------------------------------------------------------------------- */

kern_return_t bootstrap_ipc_lookup(struct task *caller,
                                    const char *name,
                                    mach_port_name_t *dst_name_out)
{
    if (!caller || !name || !dst_name_out || !bstrap_port)
        return KERN_INVALID_ARGUMENT;

    /* Give the caller a send right to the bootstrap port */
    mach_port_name_t bstrap_snd;
    kern_return_t kr = ipc_right_copy_send(bootstrap_server_task,
                                            bstrap_rcv_name,
                                            caller,
                                            &bstrap_snd);
    if (kr != KERN_SUCCESS)
        return kr;

    /* Allocate a reply port in the caller's space */
    mach_port_name_t reply_rcv;
    struct ipc_port *reply_port;
    kr = ipc_right_alloc_receive(caller, &reply_rcv, &reply_port, 1 /* also_send */);
    if (kr != KERN_SUCCESS)
        return kr;

    /* Build and send the LOOKUP request */
    bootstrap_ipc_lookup_request_t req;
    kmemset(&req, 0, sizeof(req));
    req.hdr.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                               MACH_MSG_TYPE_MAKE_SEND_ONCE);
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port = bstrap_snd;
    req.hdr.msgh_local_port  = reply_rcv;
    req.hdr.msgh_id          = BOOTSTRAP_IPC_MSG_LOOKUP;
    kstrncpy(req.name, name, BOOTSTRAP_IPC_NAME_MAX);

    kr = mach_msg_send(caller, &req.hdr, sizeof(req));
    if (kr != KERN_SUCCESS)
        return kr;

    /* Process the lookup request in the bootstrap server */
    bootstrap_ipc_run_once();

    /* Receive the reply on the reply port */
    uint8_t reply_buf[sizeof(bootstrap_ipc_lookup_reply_t) + 8];
    mach_msg_size_t rsz = 0;
    kmemset(reply_buf, 0, sizeof(reply_buf));

    kr = mach_msg_receive(caller, reply_rcv,
                           reply_buf, sizeof(reply_buf), &rsz);
    if (kr != KERN_SUCCESS)
        return kr;

    mach_msg_header_t *rhdr = (mach_msg_header_t *)reply_buf;

    if (rhdr->msgh_id == BOOTSTRAP_IPC_MSG_REPLY_ERR)
        return KERN_NAME_EXISTS; /* service not found */

    if (rhdr->msgh_id != BOOTSTRAP_IPC_MSG_REPLY_OK)
        return KERN_FAILURE;

    bootstrap_ipc_lookup_reply_t *reply =
        (bootstrap_ipc_lookup_reply_t *)reply_buf;

    *dst_name_out = reply->svc_port_desc.name;
    return (reply->result == 0) ? KERN_SUCCESS : KERN_FAILURE;
}
