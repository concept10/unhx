/*
 * servers/bootstrap/bootstrap.h — Bootstrap server public interface for NEOMACH
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
 *        bootstrap_register("com.neomach.vfs", vfs_port)
 *   4. A client looks up a service by name:
 *        bootstrap_lookup("com.neomach.vfs") → vfs_port send right
 *   5. The client now has a send right to the VFS server and can
 *      communicate directly — no further involvement from bootstrap.
 *
 * This is a clean capability distribution mechanism: bootstrap is the
 * root of trust, and all rights flow from it through explicit transfers.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §6 — Bootstrap;
 *            OSF MK bootstrap/bootstrap.h for the interface specification.
 */

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <stdint.h>

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

/*
 * bootstrap_init — initialise the bootstrap server.
 * Creates the registry and prepares to handle registration/lookup requests.
 */
void bootstrap_init(void);

/*
 * bootstrap_register — register a service port under a name.
 *
 * name: null-terminated service name (e.g. "com.neomach.vfs")
 * port: the service's port identifier (opaque handle)
 *
 * Returns BOOTSTRAP_SUCCESS or an error code.
 */
int bootstrap_register(const char *name, uint32_t port);

/*
 * bootstrap_lookup — look up a service by name.
 *
 * name:     null-terminated service name
 * out_port: receives the port handle on success
 *
 * Returns BOOTSTRAP_SUCCESS or BOOTSTRAP_UNKNOWN_SERVICE.
 */
int bootstrap_lookup(const char *name, uint32_t *out_port);

/*
 * bootstrap_checkin — claim a pre-registered service slot.
 *
 * A server that was launched by the bootstrap server itself may call
 * checkin to claim a slot that was pre-reserved with a name but no
 * port yet.
 *
 * name:     null-terminated service name
 * out_port: receives the pre-allocated port handle
 *
 * Returns BOOTSTRAP_SUCCESS or BOOTSTRAP_UNKNOWN_SERVICE.
 */
int bootstrap_checkin(const char *name, uint32_t *out_port);

#endif /* BOOTSTRAP_H */
