/*
 * kernel/ipc/ipc.c — Mach IPC subsystem initialisation for UNHOX
 *
 * This file contains the top-level initialisation of the IPC subsystem and
 * implementations of the ipc_port and ipc_space primitives declared in their
 * respective headers.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC Overview.
 */

#include "ipc.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * ipc_init
 * ------------------------------------------------------------------------- */

void ipc_init(void)
{
    /*
     * TODO: Initialise global IPC state — port zone allocator, dead-name
     * notification infrastructure, etc.  For Phase 1, individual ports and
     * spaces are initialised on allocation; no global state is required yet.
     */
}

/* -------------------------------------------------------------------------
 * ipc_port operations
 *
 * TODO: Full implementation in Phase 2 (ipc_kmsg, mach_msg entry point).
 * For now we provide the minimal stubs needed for Phase 1 self-tests.
 * ------------------------------------------------------------------------- */

struct ipc_port *ipc_port_alloc(struct task *receiver)
{
    (void)receiver;
    /* TODO: Allocate from a zone allocator; initialise all fields. */
    return NULL;
}

void ipc_port_destroy(struct ipc_port *port)
{
    (void)port;
    /* TODO: Drain message queue, notify dead-name holders, free memory. */
}

/* -------------------------------------------------------------------------
 * ipc_space operations
 * ------------------------------------------------------------------------- */

struct ipc_space *ipc_space_create(struct task *task)
{
    (void)task;
    /* TODO: Allocate and initialise a port name space. */
    return NULL;
}

void ipc_space_destroy(struct ipc_space *space)
{
    (void)space;
    /* TODO: Iterate all entries, release rights, free memory. */
}

kern_return_t ipc_space_alloc_name(struct ipc_space *space,
                                   mach_port_name_t *namep)
{
    (void)space;
    (void)namep;
    /* TODO: Walk is_table from is_next_free to find a free slot. */
    return KERN_NO_SPACE;
}

struct ipc_entry *ipc_space_lookup(struct ipc_space *space,
                                   mach_port_name_t  name)
{
    (void)space;
    (void)name;
    /* TODO: Bounds check, return &space->is_table[name] if valid. */
    return NULL;
}
