/*
 * kernel/ipc/ipc_entry.h — One slot in a task's port name space (NEOMACH)
 *
 * An ipc_entry is a single row in the ipc_space table.  Each row maps one
 * port name (a small integer visible to userspace) to an actual kernel port
 * object (ipc_port *) and records which rights the owning task holds for that
 * name.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Port Names;
 *            OSF MK ipc/ipc_entry.h for field conventions.
 */

#ifndef IPC_ENTRY_H
#define IPC_ENTRY_H

#include <stdint.h>
#include <stddef.h>
#include "mach/mach_types.h"

/* Forward declaration */
struct ipc_port;

/* -------------------------------------------------------------------------
 * Entry right bits
 *
 * An entry may carry any combination of these right bits.  In practice the
 * legal combinations are:
 *   RECEIVE only
 *   SEND only
 *   RECEIVE | SEND  (the common case when a task creates a port for itself)
 *   SEND_ONCE only
 *   PORT_SET only
 *   DEAD_NAME only  (the port was destroyed; name is kept as a tombstone)
 *
 * CMU Mach 3.0 paper §3.2: "Each entry in the port name space records the
 * set of rights associated with that name."
 * ------------------------------------------------------------------------- */

#define IE_BITS_NONE        (0u)
#define IE_BITS_SEND        (1u << 0)   /* task holds a send right          */
#define IE_BITS_RECEIVE     (1u << 1)   /* task holds the receive right      */
#define IE_BITS_SEND_ONCE   (1u << 2)   /* task holds a send-once right      */
#define IE_BITS_PORT_SET    (1u << 3)   /* entry is a port set, not a port   */
#define IE_BITS_DEAD_NAME   (1u << 4)   /* port was destroyed; dead name     */

/* User-reference count field position in ie_bits (bits 8..31) */
#define IE_BITS_UREFS_SHIFT  8
#define IE_BITS_UREFS_MASK  (0xFFFFFFu << IE_BITS_UREFS_SHIFT)
#define IE_BITS_UREFS(bits) (((bits) & IE_BITS_UREFS_MASK) >> IE_BITS_UREFS_SHIFT)

/* -------------------------------------------------------------------------
 * struct ipc_entry — one slot in the port name table
 *
 * The slot index within ipc_space.is_table is the port name the owning task
 * uses to refer to this right.  Index 0 is reserved (MACH_PORT_NULL).
 * ------------------------------------------------------------------------- */

struct ipc_entry {
    /*
     * ie_object — pointer to the kernel port object this entry refers to.
     *
     * NULL if the slot is free (ie_bits == IE_BITS_NONE) or the name is a
     * dead name (IE_BITS_DEAD_NAME set), in which case the port has already
     * been destroyed and no kernel object remains.
     */
    struct ipc_port    *ie_object;

    /*
     * ie_bits — packed right bits and user-reference count.
     *
     * The lower 8 bits hold the right-type flags (IE_BITS_*).
     * Bits 8–31 hold the user-reference count for send rights
     * (MACH_PORT_RIGHT_SEND is the only right with a urefs count in Mach).
     *
     * CMU Mach 3.0 paper §3.3: "User-reference counts allow a task to hold
     * multiple references to the same send right.  Each mach_port_deallocate
     * decrements the count; the right is released when it reaches zero."
     */
    uint32_t            ie_bits;

    /*
     * ie_index — this entry's own index in is_table, cached for convenience.
     * Equals the port name value the owning task uses.
     */
    mach_port_name_t    ie_index;
};

/* Convenience: test whether a slot is in use */
static inline int ipc_entry_is_free(const struct ipc_entry *e)
{
    return (e->ie_bits == IE_BITS_NONE) && (e->ie_object == NULL);
}

#endif /* IPC_ENTRY_H */
