/*
 * kernel/ipc/ipc.c — Mach IPC subsystem implementation for NEOMACH
 *
 * Implements ipc_port and ipc_space operations with real allocation
 * and lookup logic for Phase 1.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC Overview.
 */

#include "ipc.h"
#include "ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "kern/task.h"
#include <stddef.h>

/* -------------------------------------------------------------------------
 * ipc_init
 * ------------------------------------------------------------------------- */

void ipc_init(void)
{
    /*
     * Phase 1: No global IPC state to initialise.
     * Individual ports and spaces are initialised on allocation.
     *
     * TODO (Phase 2): Initialise port zone allocator, dead-name notification
     * infrastructure, kernel port registration, etc.
     */
}

/* -------------------------------------------------------------------------
 * ipc_port operations
 * ------------------------------------------------------------------------- */

struct ipc_port *ipc_port_alloc(struct task *receiver)
{
    struct ipc_port *port = (struct ipc_port *)kalloc(sizeof(struct ipc_port));
    if (!port)
        return (void *)0;

    /* Initialise port state */
    atomic_init(&port->ip_type, IPC_PORT_TYPE_ACTIVE);
    port->ip_lock        = (atomic_flag)ATOMIC_FLAG_INIT;
    port->ip_send_rights = 0;
    port->ip_receiver    = receiver;
    port->ip_seqno       = 0;
    port->ip_nsrequest   = (void *)0;

    /* Allocate and initialise the message queue */
    port->ip_messages = (struct ipc_mqueue *)kalloc(sizeof(struct ipc_mqueue));
    if (!port->ip_messages) {
        kfree(port);
        return (void *)0;
    }
    ipc_mqueue_init(port->ip_messages);

    return port;
}

void ipc_port_destroy(struct ipc_port *port)
{
    if (!port)
        return;

    ipc_port_lock(port);

    /*
     * Transition to dead state.
     *
     * CMU Mach 3.0 paper §3.1: "When the receive right for a port is
     * destroyed, all outstanding send rights become dead names."
     *
     * We set the type atomically so that concurrent senders can do a
     * lock-free dead-port check before acquiring ip_lock.
     */
    atomic_store_explicit(&port->ip_type, IPC_PORT_TYPE_DEAD,
                          memory_order_release);

    port->ip_receiver    = (void *)0;
    port->ip_send_rights = 0;

    /*
     * TODO: Drain the message queue and free all queued ipc_kmsgs.
     * TODO: Walk all ipc_spaces that hold send rights to this port and
     *       transition their entries to IE_BITS_DEAD_NAME.
     */

    ipc_port_unlock(port);

    /*
     * Drain all queued messages and free their OOL buffers.
     *
     * CMU Mach 3.0 paper §3.1: When the receive right is destroyed, all
     * messages still queued on the port are discarded.  In a full
     * implementation with port-set membership, this would also remove the
     * port from any port sets it belongs to.
     */
    if (port->ip_messages) {
        ipc_mqueue_drain(port->ip_messages);
        kfree(port->ip_messages);
        port->ip_messages = (void *)0;
    }

    /* Free the port memory */
    kfree(port);
}

/* -------------------------------------------------------------------------
 * ipc_space operations
 * ------------------------------------------------------------------------- */

struct ipc_space *ipc_space_create(struct task *task)
{
    struct ipc_space *space = (struct ipc_space *)kalloc(sizeof(struct ipc_space));
    if (!space)
        return (void *)0;

    space->is_lock  = (atomic_flag)ATOMIC_FLAG_INIT;
    space->is_active    = 1;
    space->is_task      = task;
    space->is_table_size = IPC_SPACE_MAX_ENTRIES;
    space->is_free_count = IPC_SPACE_MAX_ENTRIES - 1; /* slot 0 reserved */
    space->is_next_free  = IPC_SPACE_FIRST_VALID;

    /* Clear all entries */
    for (uint32_t i = 0; i < IPC_SPACE_MAX_ENTRIES; i++) {
        space->is_table[i].ie_object = (void *)0;
        space->is_table[i].ie_bits   = IE_BITS_NONE;
        space->is_table[i].ie_index  = (mach_port_name_t)i;
    }

    return space;
}

void ipc_space_destroy(struct ipc_space *space)
{
    if (!space)
        return;

    ipc_space_lock(space);
    space->is_active = 0;

    /*
     * Walk every entry and release rights.
     * For send rights: decrement ip_send_rights on the target port.
     * For receive rights: destroy the port.
     */
    for (uint32_t i = IPC_SPACE_FIRST_VALID; i < space->is_table_size; i++) {
        struct ipc_entry *e = &space->is_table[i];
        if (e->ie_bits == IE_BITS_NONE || !e->ie_object)
            continue;

        if (e->ie_bits & IE_BITS_RECEIVE) {
            /* Destroying the receive right destroys the port */
            ipc_port_destroy(e->ie_object);
        } else if (e->ie_bits & IE_BITS_SEND) {
            /* Decrement send right count on the port */
            struct ipc_port *port = e->ie_object;
            ipc_port_lock(port);
            if (port->ip_send_rights > 0)
                port->ip_send_rights--;
            ipc_port_unlock(port);
        }

        e->ie_object = (void *)0;
        e->ie_bits   = IE_BITS_NONE;
    }

    ipc_space_unlock(space);
    kfree(space);
}

kern_return_t ipc_space_alloc_name(struct ipc_space *space,
                                   mach_port_name_t *namep)
{
    if (!space || !space->is_active)
        return KERN_INVALID_TASK;

    if (space->is_free_count == 0)
        return KERN_NO_SPACE;

    /* Search from the hint forward, wrapping around */
    mach_port_name_t start = space->is_next_free;
    mach_port_name_t i = start;

    do {
        if (i >= IPC_SPACE_FIRST_VALID && i < space->is_table_size) {
            if (ipc_entry_is_free(&space->is_table[i])) {
                *namep = i;
                space->is_free_count--;

                /* Advance the free hint */
                space->is_next_free = (i + 1 < space->is_table_size)
                                          ? i + 1
                                          : IPC_SPACE_FIRST_VALID;
                return KERN_SUCCESS;
            }
        }

        i++;
        if (i >= space->is_table_size)
            i = IPC_SPACE_FIRST_VALID;
    } while (i != start);

    return KERN_NO_SPACE;
}

struct ipc_entry *ipc_space_lookup(struct ipc_space *space,
                                   mach_port_name_t  name)
{
    if (!space || !space->is_active)
        return (void *)0;

    if (name == MACH_PORT_NULL || name >= space->is_table_size)
        return (void *)0;

    struct ipc_entry *e = &space->is_table[name];

    /* A free entry is not a valid lookup result */
    if (ipc_entry_is_free(e))
        return (void *)0;

    return e;
}
