/*
 * servers/bootstrap/bootstrap.h — Bootstrap server public interface for UNHOX
 *
 * The bootstrap server is the first task started by the kernel after boot.
 * Every other server registers with it and looks up other servers through it.
 * It is the Mach equivalent of /sbin/init — but it works entirely through
 * port IPC, not system calls.
 *
 * WHY THE BOOTSTRAP SERVER EXISTS:
 *
 * When a new task is created, it has no port rights except one — a send
 * right to the bootstrap port.  Everything else is obtained by negotiating
 * through bootstrap.  This is how Mach enforces that capability distribution
 * starts from a known root:
 *
 *   1. Kernel creates bootstrap server task, gives it a receive right on
 *      the bootstrap port.
 *   2. Every new task gets a send right to the bootstrap port.
 *   3. A server registers its service port with bootstrap:
 *        BOOTSTRAP_MSG_REGISTER → "com.unhox.vfs", vfs_port
 *   4. A client looks up a service by name:
 *        BOOTSTRAP_MSG_LOOKUP → "com.unhox.vfs" → vfs_port send right
 *   5. The client now has a send right to the VFS server and can
 *      communicate directly — no further involvement from bootstrap.
 *
 * This is a clean capability distribution mechanism: bootstrap is the
 * root of trust, and all rights flow from it through explicit transfers.
 *
 * Phase 2 implementation:
 *   - Runs as a kernel thread in a blocking ipc_mqueue_receive() loop
 *   - Receives REGISTER and LOOKUP messages on its bootstrap port
 *   - Replies to LOOKUP via the reply_port field in the message body
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap.h for the interface specification.
 */

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdint.h>
#include "mach/mach_types.h"

/* Forward declaration — avoid pulling in kernel headers from server code */
struct ipc_port;

/* Maximum length of a service name */
#define BOOTSTRAP_NAME_MAX  64

/* Maximum number of registered services */
#define BOOTSTRAP_MAX_SERVICES  32

/* Return codes */
#define BOOTSTRAP_SUCCESS           0
#define BOOTSTRAP_NOT_PRIVILEGED    1
#define BOOTSTRAP_NAME_IN_USE       2
#define BOOTSTRAP_UNKNOWN_SERVICE   3
#define BOOTSTRAP_SERVICE_ACTIVE    4
#define BOOTSTRAP_NO_MEMORY         5

/* -------------------------------------------------------------------------
 * Bootstrap message IDs
 *
 * Clients send these on the bootstrap port.  The server dispatches by
 * msgh_id and processes the request.
 * ------------------------------------------------------------------------- */

#define BOOTSTRAP_MSG_REGISTER  100 /* register a service name → port */
#define BOOTSTRAP_MSG_LOOKUP    101 /* look up a service name */
#define BOOTSTRAP_MSG_REPLY     102 /* reply from bootstrap to client */

/* -------------------------------------------------------------------------
 * Bootstrap message structures
 *
 * All messages begin with a standard mach_msg_header_t.  The msgh_id field
 * selects the operation.  Port values are carried as uint64_t in the body
 * (not in the 32-bit msgh_remote/local_port fields) to hold kernel pointers.
 * ------------------------------------------------------------------------- */

/*
 * bootstrap_register_msg_t — register a service.
 *
 * Client sends this to bootstrap_port->ip_messages.
 * No reply is sent; registration is fire-and-forget.
 */
typedef struct {
    mach_msg_header_t   hdr;
    char                name[BOOTSTRAP_NAME_MAX]; /* null-terminated service name */
    uint64_t            service_port;             /* ipc_port * of the service    */
} bootstrap_register_msg_t;

/*
 * bootstrap_lookup_msg_t — look up a service by name.
 *
 * Client sends this to bootstrap_port->ip_messages.
 * Bootstrap replies via the reply_port (an ipc_port * to send on).
 */
typedef struct {
    mach_msg_header_t   hdr;
    char                name[BOOTSTRAP_NAME_MAX]; /* null-terminated service name */
    uint64_t            reply_port;               /* ipc_port * for the reply     */
} bootstrap_lookup_msg_t;

/*
 * bootstrap_reply_msg_t — lookup reply from bootstrap to client.
 *
 * Sent by bootstrap to reply_port->ip_messages.
 * retcode == BOOTSTRAP_SUCCESS and port_val is the ipc_port * on success.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             retcode;  /* BOOTSTRAP_SUCCESS or error code */
    uint32_t            _pad;
    uint64_t            port_val; /* ipc_port * on success, 0 on error */
} bootstrap_reply_msg_t;

/* -------------------------------------------------------------------------
 * Global bootstrap port
 *
 * Set by bootstrap_main() at startup.  Other kernel threads can send
 * messages here to register or look up services.
 * ------------------------------------------------------------------------- */

extern struct ipc_port *bootstrap_port;

/* -------------------------------------------------------------------------
 * Registry C API (used internally by the bootstrap thread)
 * ------------------------------------------------------------------------- */

void bootstrap_init(void);
int  bootstrap_register(const char *name, uint64_t port);
int  bootstrap_lookup(const char *name, uint64_t *out_port);
int  bootstrap_checkin(const char *name, uint64_t *out_port);

/* -------------------------------------------------------------------------
 * Thread entry point
 * ------------------------------------------------------------------------- */

/*
 * bootstrap_main — entry point for the bootstrap server thread.
 * Initialises the registry, allocates the bootstrap port, then blocks
 * in a Mach message receive loop forever.
 */
void bootstrap_main(void);

#endif /* BOOTSTRAP_H */
