/*
 * kernel/ipc/ipc_kmsg.c — mach_msg() kernel entry point for NEOMACH
 *
 * Implements the kernel-side send and receive operations that form the
 * backbone of all Mach IPC.
 *
 * SECURITY MODEL:
 * The capability invariant is enforced here: a task can only send to ports
 * for which it holds a send right in its ipc_space.  Port names are local
 * to each task's space and cannot be forged — the kernel translates them
 * to ipc_port pointers internally.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 */

#include "ipc_kmsg.h"
#include "ipc.h"
#include "ipc_mqueue.h"
#include "kern/task.h"

kern_return_t mach_msg_send(struct task *sender,
                            mach_msg_header_t *msg,
                            mach_msg_size_t size)
{
    /* Validate arguments */
    if (!sender || !msg)
        return KERN_INVALID_ARGUMENT;

    if (size < sizeof(mach_msg_header_t))
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *space = sender->t_ipc_space;
    if (!space)
        return KERN_INVALID_TASK;

    /*
     * Step 1: Look up the destination port in the sender's ipc_space.
     *
     * The message header's msgh_remote_port field contains the port NAME
     * (not a kernel pointer).  We translate it through the sender's space.
     */
    mach_port_name_t dest_name = msg->msgh_remote_port;

    ipc_space_lock(space);
    struct ipc_entry *entry = ipc_space_lookup(space, dest_name);

    if (!entry) {
        ipc_space_unlock(space);
        return KERN_INVALID_NAME;
    }

    /*
     * Step 2: Verify the sender holds a SEND right (or SEND_ONCE right)
     * to the destination port.
     *
     * This is the CAPABILITY CHECK — the core of Mach's security model.
     * A task that does not hold a send right simply cannot send to a port,
     * regardless of whether it knows the port name.
     */
    if (!(entry->ie_bits & (IE_BITS_SEND | IE_BITS_SEND_ONCE))) {
        ipc_space_unlock(space);
        return KERN_INVALID_RIGHT;
    }

    struct ipc_port *port = entry->ie_object;
    ipc_space_unlock(space);

    if (!port)
        return KERN_INVALID_NAME;

    /*
     * Step 3: Check that the port is still alive.
     */
    int port_type = atomic_load_explicit(&port->ip_type, memory_order_acquire);
    if (port_type == IPC_PORT_TYPE_DEAD)
        return KERN_FAILURE;

    /*
     * Step 4: Enqueue the message on the port's message queue.
     *
     * Phase 1 uses copy semantics: the message is copied into a kernel
     * buffer (ipc_kmsg) inside the queue.
     */
    mach_msg_return_t mr = ipc_mqueue_send(port->ip_messages,
                                            msg, size);

    if (mr != MACH_MSG_SUCCESS)
        return KERN_FAILURE;

    return KERN_SUCCESS;
}

kern_return_t mach_msg_receive(struct task *receiver,
                               mach_port_name_t port_name,
                               void *buf,
                               mach_msg_size_t buf_size,
                               mach_msg_size_t *out_size)
{
    if (!receiver || !buf)
        return KERN_INVALID_ARGUMENT;

    struct ipc_space *space = receiver->t_ipc_space;
    if (!space)
        return KERN_INVALID_TASK;

    /*
     * Look up the port name in the receiver's space.
     */
    ipc_space_lock(space);
    struct ipc_entry *entry = ipc_space_lookup(space, port_name);

    if (!entry) {
        ipc_space_unlock(space);
        return KERN_INVALID_NAME;
    }

    /*
     * Verify the receiver holds the RECEIVE right.
     *
     * Only the holder of the receive right may dequeue messages from a port.
     * This is exclusive — exactly one task holds the receive right at any time.
     */
    if (!(entry->ie_bits & IE_BITS_RECEIVE)) {
        ipc_space_unlock(space);
        return KERN_NOT_RECEIVER;
    }

    struct ipc_port *port = entry->ie_object;
    ipc_space_unlock(space);

    if (!port || !port->ip_messages)
        return KERN_FAILURE;

    /*
     * Dequeue a message from the port's queue.
     */
    mach_msg_return_t mr = ipc_mqueue_receive(port->ip_messages,
                                               buf, buf_size, out_size);

    if (mr != MACH_MSG_SUCCESS)
        return KERN_FAILURE;

    return KERN_SUCCESS;
}
