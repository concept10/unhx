/*
 * kernel/ipc/ipc_right.c — Mach port right management for UNHOX
 *
 * Port rights are the capability tokens of the Mach security model.
 * This file implements the operations for managing port rights within
 * and across task port name spaces:
 *
 *   ipc_right_alloc_receive  — create a new port and install a RECEIVE right
 *   ipc_right_copy_send      — copy a SEND right into another task's space
 *   ipc_right_make_send_once — create a SEND_ONCE right from a RECEIVE right
 *   ipc_right_deallocate     — decrement user-reference count / release right
 *   ipc_right_transfer       — move a right from one task's space to another
 *
 * All operations enforce the capability invariant:
 *   - Only the holder of a RECEIVE right may make SEND or SEND_ONCE rights.
 *   - Only the holder of a SEND right may copy it to another task.
 *   - Transferring a right removes it from the source task's space.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.3 — Port Rights;
 *            OSF MK ipc/ipc_right.c for naming conventions and algorithms.
 *
 * L4 Performance Influence:
 *   The right management layer is designed to minimise per-operation overhead.
 *   Right lookup is O(1) in the flat Phase 1 table, and lock hold times are
 *   kept short (acquire space lock → read entry → release lock) to reduce
 *   contention, following L4's principle that IPC paths must be auditably
 *   fast.  See docs/research/ipc-performance.md for analysis.
 */

#include "ipc_right.h"
#include "ipc.h"
#include "ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "kern/task.h"

/* Serial output (from platform layer) */
extern void serial_putstr(const char *s);

/* -------------------------------------------------------------------------
 * ipc_right_alloc_receive
 *
 * Create a new Mach port and install a RECEIVE right for it into the
 * calling task's port space.  Optionally also installs a SEND right in
 * the same entry (RECEIVE | SEND is the common case when a server creates
 * a port that it will also reply on).
 *
 * CMU Mach 3.0 paper §3.3: "A task creates a port by allocating a receive
 * right.  The kernel creates the port object and places the receive right
 * in the task's name space."
 *
 * On success: *namep is set to the port name in task's space, and
 *             *portp (if non-NULL) receives the kernel port pointer.
 * Returns KERN_SUCCESS or an error code.
 * ------------------------------------------------------------------------- */
kern_return_t
ipc_right_alloc_receive(struct task *task,
                        mach_port_name_t *namep,
                        struct ipc_port **portp,
                        int also_send)
{
    if (!task || !namep)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *space = task->t_ipc_space;
    if (!space || !space->is_active)
        return KERN_INVALID_TASK;

    /* Allocate the kernel port object */
    struct ipc_port *port = ipc_port_alloc(task);
    if (!port)
        return KERN_RESOURCE_SHORTAGE;

    /* Acquire the space lock and reserve a name */
    ipc_space_lock(space);

    mach_port_name_t name;
    kern_return_t kr = ipc_space_alloc_name(space, &name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(space);
        ipc_port_destroy(port);
        return kr;
    }

    /* Install the right(s) */
    uint32_t bits = IE_BITS_RECEIVE;
    if (also_send) {
        bits |= IE_BITS_SEND;
        /* Set urefs = 1 for the initial send right */
        bits |= (1u << IE_BITS_UREFS_SHIFT);

        ipc_port_lock(port);
        port->ip_send_rights++;
        ipc_port_unlock(port);
    }

    space->is_table[name].ie_object = port;
    space->is_table[name].ie_bits   = bits;

    ipc_space_unlock(space);

    *namep = name;
    if (portp)
        *portp = port;

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * ipc_right_copy_send
 *
 * Copy a SEND right from src_task's space (at src_name) into dst_task's
 * space.  On success *dst_namep receives the name in dst_task's space.
 *
 * CMU Mach 3.0 paper §3.3: "Send rights are copyable.  When a task
 * copies a send right, the kernel increments the send-right reference
 * count on the port."
 *
 * The source right is NOT consumed (compare with ipc_right_transfer,
 * which moves the right).
 * ------------------------------------------------------------------------- */
kern_return_t
ipc_right_copy_send(struct task *src_task,
                    mach_port_name_t src_name,
                    struct task *dst_task,
                    mach_port_name_t *dst_namep)
{
    if (!src_task || !dst_task || !dst_namep)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *src_space = src_task->t_ipc_space;
    struct ipc_space *dst_space = dst_task->t_ipc_space;

    if (!src_space || !src_space->is_active)
        return KERN_INVALID_TASK;
    if (!dst_space || !dst_space->is_active)
        return KERN_INVALID_TASK;

    /* Look up the source right */
    ipc_space_lock(src_space);
    struct ipc_entry *src_entry = ipc_space_lookup(src_space, src_name);

    if (!src_entry) {
        ipc_space_unlock(src_space);
        return KERN_INVALID_NAME;
    }

    /* Verify the source holds a SEND right */
    if (!(src_entry->ie_bits & IE_BITS_SEND)) {
        ipc_space_unlock(src_space);
        return KERN_INVALID_RIGHT;
    }

    struct ipc_port *port = src_entry->ie_object;
    ipc_space_unlock(src_space);

    if (!port)
        return KERN_INVALID_NAME;

    /* Port must be alive */
    if (atomic_load_explicit(&port->ip_type, memory_order_acquire)
            == IPC_PORT_TYPE_DEAD)
        return KERN_INVALID_RIGHT;

    /* Reserve a name in the destination space */
    ipc_space_lock(dst_space);
    mach_port_name_t dst_name;
    kern_return_t kr = ipc_space_alloc_name(dst_space, &dst_name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(dst_space);
        return kr;
    }

    /* Install SEND right in dst_space */
    dst_space->is_table[dst_name].ie_object = port;
    dst_space->is_table[dst_name].ie_bits   =
        IE_BITS_SEND | (1u << IE_BITS_UREFS_SHIFT);

    ipc_space_unlock(dst_space);

    /* Increment the port's send-right reference count */
    ipc_port_lock(port);
    port->ip_send_rights++;
    ipc_port_unlock(port);

    *dst_namep = dst_name;
    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * ipc_right_make_send_once
 *
 * Create a SEND_ONCE right for port_name in task's space and insert it
 * into dst_task's space.
 *
 * CMU Mach 3.0 paper §3.3: "A send-once right allows exactly one message
 * to be sent.  It is consumed by the send operation."
 *
 * The caller must hold the RECEIVE right for port_name.
 * ------------------------------------------------------------------------- */
kern_return_t
ipc_right_make_send_once(struct task *task,
                         mach_port_name_t port_name,
                         struct task *dst_task,
                         mach_port_name_t *dst_namep)
{
    if (!task || !dst_task || !dst_namep)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *space     = task->t_ipc_space;
    struct ipc_space *dst_space = dst_task->t_ipc_space;

    if (!space || !space->is_active)
        return KERN_INVALID_TASK;
    if (!dst_space || !dst_space->is_active)
        return KERN_INVALID_TASK;

    /* Look up the source entry and verify RECEIVE right */
    ipc_space_lock(space);
    struct ipc_entry *entry = ipc_space_lookup(space, port_name);

    if (!entry) {
        ipc_space_unlock(space);
        return KERN_INVALID_NAME;
    }

    if (!(entry->ie_bits & IE_BITS_RECEIVE)) {
        ipc_space_unlock(space);
        return KERN_INVALID_RIGHT;
    }

    struct ipc_port *port = entry->ie_object;
    ipc_space_unlock(space);

    if (!port)
        return KERN_INVALID_NAME;

    if (atomic_load_explicit(&port->ip_type, memory_order_acquire)
            == IPC_PORT_TYPE_DEAD)
        return KERN_INVALID_RIGHT;

    /* Reserve a name in the destination space */
    ipc_space_lock(dst_space);
    mach_port_name_t dst_name;
    kern_return_t kr = ipc_space_alloc_name(dst_space, &dst_name);
    if (kr != KERN_SUCCESS) {
        ipc_space_unlock(dst_space);
        return kr;
    }

    /* Install SEND_ONCE right — urefs is always 1 for send-once */
    dst_space->is_table[dst_name].ie_object = port;
    dst_space->is_table[dst_name].ie_bits   = IE_BITS_SEND_ONCE;

    ipc_space_unlock(dst_space);

    *dst_namep = dst_name;
    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * ipc_right_deallocate
 *
 * Release one user reference to the right held under name in task's space.
 *
 * CMU Mach 3.0 paper §3.3: "User-reference counts allow a task to hold
 * multiple references to the same send right.  Each mach_port_deallocate
 * decrements the count; the right is released when it reaches zero."
 *
 * For SEND rights: decrement urefs; when urefs == 0, remove the entry and
 *   decrement ip_send_rights on the port.
 * For RECEIVE rights: destroy the port (transitions all send rights to dead
 *   names).
 * For SEND_ONCE rights: always consumed on first deallocate.
 * ------------------------------------------------------------------------- */
kern_return_t
ipc_right_deallocate(struct task *task, mach_port_name_t name)
{
    if (!task)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *space = task->t_ipc_space;
    if (!space || !space->is_active)
        return KERN_INVALID_TASK;

    ipc_space_lock(space);
    struct ipc_entry *entry = ipc_space_lookup(space, name);

    if (!entry) {
        ipc_space_unlock(space);
        return KERN_INVALID_NAME;
    }

    uint32_t bits = entry->ie_bits;
    struct ipc_port *port = entry->ie_object;

    if (bits & IE_BITS_RECEIVE) {
        /*
         * Releasing the RECEIVE right destroys the port.
         * We must drop the space lock before calling ipc_port_destroy
         * to avoid a lock-order inversion (ipc_port_destroy acquires ip_lock).
         */
        entry->ie_object = (void *)0;
        entry->ie_bits   = IE_BITS_NONE;
        space->is_free_count++;
        ipc_space_unlock(space);

        ipc_port_destroy(port);
        return KERN_SUCCESS;
    }

    if (bits & IE_BITS_SEND) {
        uint32_t urefs = IE_BITS_UREFS(bits);
        if (urefs > 1) {
            /* Decrement user-reference count */
            entry->ie_bits = (bits & ~IE_BITS_UREFS_MASK)
                             | ((urefs - 1) << IE_BITS_UREFS_SHIFT);
            ipc_space_unlock(space);
        } else {
            /* Last reference: remove entry */
            entry->ie_object = (void *)0;
            entry->ie_bits   = IE_BITS_NONE;
            space->is_free_count++;
            ipc_space_unlock(space);

            /* Decrement the port's global send-right count */
            if (port &&
                atomic_load_explicit(&port->ip_type, memory_order_acquire)
                    != IPC_PORT_TYPE_DEAD) {
                ipc_port_lock(port);
                if (port->ip_send_rights > 0)
                    port->ip_send_rights--;
                ipc_port_unlock(port);
            }
        }
        return KERN_SUCCESS;
    }

    if (bits & IE_BITS_SEND_ONCE) {
        /* SEND_ONCE rights are always single-use; remove unconditionally */
        entry->ie_object = (void *)0;
        entry->ie_bits   = IE_BITS_NONE;
        space->is_free_count++;
        ipc_space_unlock(space);
        return KERN_SUCCESS;
    }

    /* Dead name or unknown — just remove it */
    entry->ie_object = (void *)0;
    entry->ie_bits   = IE_BITS_NONE;
    space->is_free_count++;
    ipc_space_unlock(space);
    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * ipc_right_transfer
 *
 * Move a right from src_task (at src_name) into dst_task's space.
 *
 * After the transfer:
 *   - The right is removed from src_task's space (src_name becomes free).
 *   - The right is installed in dst_task's space under a new name.
 *   - *dst_namep receives the new name.
 *
 * This is the underlying mechanism used by the kernel when a message carries
 * a port right in a typed descriptor (MACH_MSG_TYPE_MOVE_SEND, etc.).
 *
 * CMU Mach 3.0 paper §3.4: "A task may send a port right inside a message
 * using a typed message descriptor.  The kernel removes the right from the
 * sender's space and installs it in the receiver's space atomically."
 * ------------------------------------------------------------------------- */
kern_return_t
ipc_right_transfer(struct task *src_task,
                   mach_port_name_t src_name,
                   struct task *dst_task,
                   mach_port_name_t *dst_namep)
{
    if (!src_task || !dst_task || !dst_namep)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *src_space = src_task->t_ipc_space;
    struct ipc_space *dst_space = dst_task->t_ipc_space;

    if (!src_space || !src_space->is_active)
        return KERN_INVALID_TASK;
    if (!dst_space || !dst_space->is_active)
        return KERN_INVALID_TASK;

    /* Read the source entry */
    ipc_space_lock(src_space);
    struct ipc_entry *src_entry = ipc_space_lookup(src_space, src_name);

    if (!src_entry) {
        ipc_space_unlock(src_space);
        return KERN_INVALID_NAME;
    }

    uint32_t bits = src_entry->ie_bits;
    struct ipc_port *port = src_entry->ie_object;

    /* We can transfer SEND, SEND_ONCE, or RECEIVE rights */
    if (!(bits & (IE_BITS_SEND | IE_BITS_SEND_ONCE | IE_BITS_RECEIVE))) {
        ipc_space_unlock(src_space);
        return KERN_INVALID_RIGHT;
    }

    /* Snapshot the bits to install in dst */
    uint32_t dst_bits = bits;

    /* Remove from source */
    src_entry->ie_object = (void *)0;
    src_entry->ie_bits   = IE_BITS_NONE;
    src_space->is_free_count++;
    ipc_space_unlock(src_space);

    /* Install in destination */
    ipc_space_lock(dst_space);
    mach_port_name_t dst_name;
    kern_return_t kr = ipc_space_alloc_name(dst_space, &dst_name);
    if (kr != KERN_SUCCESS) {
        /*
         * Destination space is full.  We've already removed the right
         * from the source.  Restore it to prevent capability loss.
         *
         * TODO: A more robust implementation would reserve the destination
         * slot before removing from the source, or use a 2-phase protocol.
         */
        ipc_space_unlock(dst_space);

        serial_putstr("[WARN] ipc_right_transfer: dst space full; "
                      "restoring right to src\r\n");

        ipc_space_lock(src_space);
        src_space->is_table[src_name].ie_object = port;
        src_space->is_table[src_name].ie_bits   = bits;
        src_space->is_free_count--;
        ipc_space_unlock(src_space);

        return kr;
    }

    dst_space->is_table[dst_name].ie_object = port;
    dst_space->is_table[dst_name].ie_bits   = dst_bits;
    ipc_space_unlock(dst_space);

    /* If transferring the RECEIVE right, update the port's receiver */
    if (bits & IE_BITS_RECEIVE) {
        ipc_port_lock(port);
        port->ip_receiver = dst_task;
        ipc_port_unlock(port);
    }

    *dst_namep = dst_name;
    return KERN_SUCCESS;
}
