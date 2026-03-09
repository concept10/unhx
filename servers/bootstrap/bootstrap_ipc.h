/*
 * servers/bootstrap/bootstrap_ipc.h — IPC-based bootstrap server for NEOMACH
 *
 * Phase 2 enhancement to the bootstrap server: services are now registered
 * and looked up via real Mach IPC messages, not a direct C API.
 *
 * A client sends a BOOTSTRAP_IPC_REGISTER or BOOTSTRAP_IPC_LOOKUP request
 * message to the global bootstrap port.  The bootstrap server processes the
 * request and sends a reply carrying the service send right (for lookup) or
 * a result code (for registration).
 *
 * Message IDs:
 *   BOOTSTRAP_IPC_MSG_REGISTER — register a service port by name
 *   BOOTSTRAP_IPC_MSG_LOOKUP   — look up a service port by name
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap_defs.h for the original interface.
 */

#ifndef BOOTSTRAP_IPC_H
#define BOOTSTRAP_IPC_H

#include "mach/mach_types.h"
#include "ipc/ipc_port.h"

/* Forward declaration */
struct task;

/* -------------------------------------------------------------------------
 * Bootstrap IPC message IDs
 * ------------------------------------------------------------------------- */
#define BOOTSTRAP_IPC_MSG_REGISTER   ((mach_msg_id_t) 400)
#define BOOTSTRAP_IPC_MSG_LOOKUP     ((mach_msg_id_t) 401)
#define BOOTSTRAP_IPC_MSG_REPLY_OK   ((mach_msg_id_t) 402)
#define BOOTSTRAP_IPC_MSG_REPLY_ERR  ((mach_msg_id_t) 403)

/* Maximum service name length */
#define BOOTSTRAP_IPC_NAME_MAX       64

/* -------------------------------------------------------------------------
 * Bootstrap IPC message types
 * ------------------------------------------------------------------------- */

/*
 * bootstrap_ipc_register_request_t — sent to bootstrap_port to register a
 * service.  Carries the service port as a COPY_SEND port descriptor.
 */
typedef struct {
    mach_msg_header_t           hdr;            /* msgh_id = BOOTSTRAP_IPC_MSG_REGISTER */
    mach_msg_body_t             body;
    mach_msg_port_descriptor_t  svc_port_desc;  /* MACH_MSG_TYPE_COPY_SEND              */
    char                        name[BOOTSTRAP_IPC_NAME_MAX];
} bootstrap_ipc_register_request_t;

/*
 * bootstrap_ipc_lookup_request_t — sent to bootstrap_port to look up a
 * service by name.  msgh_local_port carries the reply port.
 */
typedef struct {
    mach_msg_header_t hdr;                      /* msgh_id = BOOTSTRAP_IPC_MSG_LOOKUP */
    char              name[BOOTSTRAP_IPC_NAME_MAX];
} bootstrap_ipc_lookup_request_t;

/*
 * bootstrap_ipc_lookup_reply_t — sent by bootstrap to the client's reply
 * port on a successful lookup.  Carries the service send right.
 */
typedef struct {
    mach_msg_header_t           hdr;            /* msgh_id = BOOTSTRAP_IPC_MSG_REPLY_OK */
    mach_msg_body_t             body;
    mach_msg_port_descriptor_t  svc_port_desc;  /* MACH_MSG_TYPE_COPY_SEND              */
    int32_t                     result;
} bootstrap_ipc_lookup_reply_t;

/*
 * bootstrap_ipc_error_reply_t — sent by bootstrap on error.
 */
typedef struct {
    mach_msg_header_t hdr;   /* msgh_id = BOOTSTRAP_IPC_MSG_REPLY_ERR */
    int32_t           result;
} bootstrap_ipc_error_reply_t;

/* -------------------------------------------------------------------------
 * Server lifecycle
 * ------------------------------------------------------------------------- */

/*
 * bootstrap_ipc_init — create the bootstrap server task and allocate the
 * global bootstrap port.
 *
 * Must be called after ipc_init() and task_create() are available.
 * After this call, bootstrap_ipc_port() returns a valid send right.
 */
void bootstrap_ipc_init(void);

/*
 * bootstrap_ipc_port — return the global bootstrap port.
 *
 * Returns NULL before bootstrap_ipc_init() is called.
 */
struct ipc_port *bootstrap_ipc_port(void);

/*
 * bootstrap_ipc_port_name — return the bootstrap port name in
 * bootstrap_server_task's ipc_space.
 */
mach_port_name_t bootstrap_ipc_port_name(void);

/*
 * bootstrap_ipc_run_once — process one pending message from the bootstrap
 * port queue.
 *
 * Returns 1 if a message was processed, 0 if the queue was empty.
 */
int bootstrap_ipc_run_once(void);

/* -------------------------------------------------------------------------
 * Client API (using real Mach IPC)
 * ------------------------------------------------------------------------- */

/*
 * bootstrap_ipc_register — register a service port via IPC.
 *
 * sender:     the task registering the service (must hold a send right to
 *             the bootstrap port, and a send right to svc_port_name)
 * svc_port_name: the port name in sender's space to register
 * name:       null-terminated service name string
 *
 * Internally: sends a BOOTSTRAP_IPC_MSG_REGISTER message to the bootstrap
 * port, then calls bootstrap_ipc_run_once() to process it synchronously.
 *
 * Returns KERN_SUCCESS or an error code.
 */
kern_return_t bootstrap_ipc_register(struct task *sender,
                                      mach_port_name_t svc_port_name,
                                      const char *name);

/*
 * bootstrap_ipc_lookup — look up a service port via IPC.
 *
 * caller:       the task performing the lookup
 * name:         null-terminated service name to look up
 * dst_name_out: OUT — port name of the looked-up service in caller's space
 *
 * Internally: sends a BOOTSTRAP_IPC_MSG_LOOKUP message to the bootstrap
 * port, calls bootstrap_ipc_run_once() to process it, then receives the
 * reply carrying the service send right.
 *
 * Returns KERN_SUCCESS or an error code.
 */
kern_return_t bootstrap_ipc_lookup(struct task *caller,
                                    const char *name,
                                    mach_port_name_t *dst_name_out);

#endif /* BOOTSTRAP_IPC_H */
