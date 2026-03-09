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
 * PHASE 2 ADDITIONS:
 * Complex message support (MACH_MSGH_BITS_COMPLEX):
 *   - Port right transfer: sender's port names are translated to ipc_port*
 *     pointers at send time and installed in the receiver's ipc_space at
 *     receive time.
 *   - OOL memory: sender's buffers are physically copied into kernel-allocated
 *     regions at send time and delivered to the receiver at receive time.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 */

#include "ipc_kmsg.h"
#include "ipc.h"
#include "ipc_mqueue.h"
#include "ipc_right.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "kern/task.h"

/* -------------------------------------------------------------------------
 * Complex message helpers
 * ------------------------------------------------------------------------- */

/*
 * process_send_descriptors — process typed descriptors in a complex message.
 *
 * Scans the message body for port and OOL descriptors.  For each:
 *   Port descriptor: extracts the port right from the sender's space based
 *     on the disposition, stores the kernel port pointer in the kmsg, and
 *     zeroes the name field so the receive side can fill it in.
 *   OOL descriptor: copies the buffer into a kalloc'd region, stores the
 *     pointer in the kmsg, and zeroes the address field.
 *
 * Returns KERN_SUCCESS or an error (caller must roll back on error).
 */
static kern_return_t
process_send_descriptors(struct task *sender,
                          struct ipc_kmsg *kmsg)
{
    mach_msg_header_t *hdr = (mach_msg_header_t *)kmsg->ikm_data;

    /* Only process if MACH_MSGH_BITS_COMPLEX is set */
    if (!(hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX))
        return KERN_SUCCESS;

    if (kmsg->ikm_size < sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t))
        return KERN_INVALID_ARGUMENT;

    mach_msg_body_t *body = (mach_msg_body_t *)(hdr + 1);
    uint32_t desc_count = body->msgh_descriptor_count;

    /* Pointer to the first descriptor, immediately after the body */
    uint8_t *desc_ptr = (uint8_t *)(body + 1);
    uint8_t *msg_end  = kmsg->ikm_data + kmsg->ikm_size;

    kmsg->ikm_n_ports = 0;
    kmsg->ikm_n_ool   = 0;

    for (uint32_t i = 0; i < desc_count; i++) {
        if (desc_ptr + sizeof(mach_msg_type_descriptor_t) > msg_end)
            return KERN_INVALID_ARGUMENT;

        uint32_t dtype = ((mach_msg_type_descriptor_t *)desc_ptr)->type;

        if (dtype == MACH_MSG_PORT_DESCRIPTOR) {
            mach_msg_port_descriptor_t *pd =
                (mach_msg_port_descriptor_t *)desc_ptr;

            if (desc_ptr + sizeof(*pd) > msg_end)
                return KERN_INVALID_ARGUMENT;

            if (kmsg->ikm_n_ports >= IPC_KMSG_MAX_PORTS)
                return KERN_RESOURCE_SHORTAGE;

            mach_port_name_t pname = pd->name;
            mach_msg_type_name_t disp = pd->disposition;

            /* Translate port name to kernel pointer based on disposition */
            struct ipc_space *space = sender->t_ipc_space;
            struct ipc_port  *port  = (void *)0;

            ipc_space_lock(space);
            struct ipc_entry *e = ipc_space_lookup(space, pname);

            if (!e) {
                ipc_space_unlock(space);
                return KERN_INVALID_NAME;
            }

            port = e->ie_object;

            switch (disp) {
            case MACH_MSG_TYPE_COPY_SEND:
                /* Retain a send right in sender's space; receiver gets a copy */
                if (!(e->ie_bits & IE_BITS_SEND)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                ipc_space_unlock(space);
                if (port) {
                    ipc_port_lock(port);
                    port->ip_send_rights++;
                    ipc_port_unlock(port);
                }
                break;

            case MACH_MSG_TYPE_MOVE_SEND:
                /* Consume one send uref from sender's space */
                if (!(e->ie_bits & IE_BITS_SEND)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                {
                    uint32_t urefs = IE_BITS_UREFS(e->ie_bits);
                    if (urefs > 1) {
                        e->ie_bits = (e->ie_bits & ~IE_BITS_UREFS_MASK)
                                     | ((urefs - 1) << IE_BITS_UREFS_SHIFT);
                    } else {
                        e->ie_object = (void *)0;
                        e->ie_bits   = IE_BITS_NONE;
                        space->is_free_count++;
                    }
                }
                ipc_space_unlock(space);
                /* ip_send_rights unchanged — right moves to receiver */
                break;

            case MACH_MSG_TYPE_MOVE_SEND_ONCE:
                if (!(e->ie_bits & IE_BITS_SEND_ONCE)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                e->ie_object = (void *)0;
                e->ie_bits   = IE_BITS_NONE;
                space->is_free_count++;
                ipc_space_unlock(space);
                break;

            case MACH_MSG_TYPE_MOVE_RECEIVE:
                if (!(e->ie_bits & IE_BITS_RECEIVE)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                /* Strip RECEIVE (and SEND if combined) from sender */
                e->ie_bits &= ~(IE_BITS_RECEIVE | IE_BITS_SEND);
                if (e->ie_bits == IE_BITS_NONE || IE_BITS_UREFS(e->ie_bits) == 0) {
                    e->ie_object = (void *)0;
                    e->ie_bits   = IE_BITS_NONE;
                    space->is_free_count++;
                }
                ipc_space_unlock(space);
                /* Update receiver pointer after installing in dst space */
                break;

            case MACH_MSG_TYPE_MAKE_SEND:
                /* Must hold RECEIVE right; creates a new send right for receiver */
                if (!(e->ie_bits & IE_BITS_RECEIVE)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                ipc_space_unlock(space);
                if (port) {
                    ipc_port_lock(port);
                    port->ip_send_rights++;
                    ipc_port_unlock(port);
                }
                /* Disposition for receiver becomes MOVE_SEND (one send right) */
                disp = MACH_MSG_TYPE_MOVE_SEND;
                break;

            case MACH_MSG_TYPE_MAKE_SEND_ONCE:
                if (!(e->ie_bits & IE_BITS_RECEIVE)) {
                    ipc_space_unlock(space);
                    return KERN_INVALID_RIGHT;
                }
                ipc_space_unlock(space);
                /* No ip_send_rights adjustment for SEND_ONCE */
                disp = MACH_MSG_TYPE_MOVE_SEND_ONCE;
                break;

            default:
                ipc_space_unlock(space);
                return KERN_INVALID_VALUE;
            }

            /* Store in kmsg */
            kmsg->ikm_ports[kmsg->ikm_n_ports].port        = port;
            kmsg->ikm_ports[kmsg->ikm_n_ports].disposition = disp;
            kmsg->ikm_n_ports++;

            /* Zero the name in the in-flight message (receiver fills it in) */
            pd->name = MACH_PORT_NULL;

            desc_ptr += sizeof(mach_msg_port_descriptor_t);

        } else if (dtype == MACH_MSG_OOL_DESCRIPTOR) {
            mach_msg_ool_descriptor_t *od =
                (mach_msg_ool_descriptor_t *)desc_ptr;

            if (desc_ptr + sizeof(*od) > msg_end)
                return KERN_INVALID_ARGUMENT;

            if (kmsg->ikm_n_ool >= IPC_KMSG_MAX_OOL)
                return KERN_RESOURCE_SHORTAGE;

            mach_msg_size_t ool_size = od->size;
            void           *ool_src  = od->address;

            if (ool_size > 0 && ool_src) {
                void *ool_buf = kalloc(ool_size);
                if (!ool_buf)
                    return KERN_RESOURCE_SHORTAGE;
                kmemcpy(ool_buf, ool_src, ool_size);

                kmsg->ikm_ool[kmsg->ikm_n_ool].buf  = ool_buf;
                kmsg->ikm_ool[kmsg->ikm_n_ool].size = ool_size;
            } else {
                kmsg->ikm_ool[kmsg->ikm_n_ool].buf  = (void *)0;
                kmsg->ikm_ool[kmsg->ikm_n_ool].size = 0;
            }
            kmsg->ikm_n_ool++;

            /* Zero the address in the in-flight message */
            od->address = (void *)0;

            desc_ptr += sizeof(mach_msg_ool_descriptor_t);

        } else {
            /* Unknown descriptor type — skip (best effort) */
            /* We can't safely advance without knowing the size; bail */
            return KERN_INVALID_VALUE;
        }
    }

    return KERN_SUCCESS;
}

/*
 * process_recv_descriptors — install port rights and OOL addresses in
 * a received complex message.
 *
 * Scans the message body for port and OOL descriptors (in the same order
 * as process_send_descriptors populated ikm_ports[] and ikm_ool[]).
 * Installs each port right into the receiver's ipc_space and fills in
 * the descriptor's name/address fields.
 */
static kern_return_t
process_recv_descriptors(struct task *receiver, struct ipc_kmsg *kmsg)
{
    mach_msg_header_t *hdr = (mach_msg_header_t *)kmsg->ikm_data;

    if (!(hdr->msgh_bits & MACH_MSGH_BITS_COMPLEX))
        return KERN_SUCCESS;

    if (kmsg->ikm_size < sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t))
        return KERN_SUCCESS;

    mach_msg_body_t *body = (mach_msg_body_t *)(hdr + 1);
    uint32_t desc_count = body->msgh_descriptor_count;

    uint8_t *desc_ptr = (uint8_t *)(body + 1);
    uint8_t *msg_end  = kmsg->ikm_data + kmsg->ikm_size;

    uint8_t port_idx = 0;
    uint8_t ool_idx  = 0;

    struct ipc_space *dst_space = receiver->t_ipc_space;

    for (uint32_t i = 0; i < desc_count; i++) {
        if (desc_ptr + sizeof(mach_msg_type_descriptor_t) > msg_end)
            break;

        uint32_t dtype = ((mach_msg_type_descriptor_t *)desc_ptr)->type;

        if (dtype == MACH_MSG_PORT_DESCRIPTOR) {
            mach_msg_port_descriptor_t *pd =
                (mach_msg_port_descriptor_t *)desc_ptr;

            if (desc_ptr + sizeof(*pd) > msg_end)
                break;

            if (port_idx < kmsg->ikm_n_ports) {
                struct ipc_port      *port = kmsg->ikm_ports[port_idx].port;
                mach_msg_type_name_t  disp = kmsg->ikm_ports[port_idx].disposition;

                if (port) {
                    mach_port_name_t dst_name = MACH_PORT_NULL;

                    ipc_space_lock(dst_space);
                    kern_return_t kr = ipc_space_alloc_name(dst_space, &dst_name);
                    if (kr == KERN_SUCCESS) {
                        uint32_t bits = IE_BITS_NONE;
                        switch (disp) {
                        case MACH_MSG_TYPE_MOVE_SEND:
                        case MACH_MSG_TYPE_COPY_SEND:
                            bits = IE_BITS_SEND | (1u << IE_BITS_UREFS_SHIFT);
                            break;
                        case MACH_MSG_TYPE_MOVE_SEND_ONCE:
                            bits = IE_BITS_SEND_ONCE;
                            break;
                        case MACH_MSG_TYPE_MOVE_RECEIVE:
                            bits = IE_BITS_RECEIVE;
                            /* Update ip_receiver to the new holder */
                            ipc_port_lock(port);
                            port->ip_receiver = receiver;
                            ipc_port_unlock(port);
                            break;
                        default:
                            bits = IE_BITS_SEND | (1u << IE_BITS_UREFS_SHIFT);
                            break;
                        }
                        dst_space->is_table[dst_name].ie_object = port;
                        dst_space->is_table[dst_name].ie_bits   = bits;
                    }
                    ipc_space_unlock(dst_space);

                    pd->name = dst_name;
                } else {
                    pd->name = MACH_PORT_NULL;
                }

                port_idx++;
            }

            desc_ptr += sizeof(mach_msg_port_descriptor_t);

        } else if (dtype == MACH_MSG_OOL_DESCRIPTOR) {
            mach_msg_ool_descriptor_t *od =
                (mach_msg_ool_descriptor_t *)desc_ptr;

            if (desc_ptr + sizeof(*od) > msg_end)
                break;

            if (ool_idx < kmsg->ikm_n_ool) {
                od->address = kmsg->ikm_ool[ool_idx].buf;
                od->size    = kmsg->ikm_ool[ool_idx].size;
                /* Caller owns the buffer; clear ikm_ool so drain won't double-free */
                kmsg->ikm_ool[ool_idx].buf  = (void *)0;
                kmsg->ikm_ool[ool_idx].size = 0;
                ool_idx++;
            }

            desc_ptr += sizeof(mach_msg_ool_descriptor_t);

        } else {
            /* Unknown descriptor — stop processing */
            break;
        }
    }

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * mach_msg_send
 * ------------------------------------------------------------------------- */

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
     * Step 4: Allocate a kernel message buffer and copy the message data.
     *
     * We allocate the kmsg here so that process_send_descriptors can
     * populate the port/OOL tracking fields directly in the kmsg.
     */
    if (size > IPC_MQUEUE_MAX_MSG_SIZE)
        return KERN_RESOURCE_SHORTAGE;

    struct ipc_kmsg *kmsg = (struct ipc_kmsg *)kalloc(sizeof(struct ipc_kmsg));
    if (!kmsg)
        return KERN_RESOURCE_SHORTAGE;

    kmsg->ikm_size    = size;
    kmsg->ikm_next    = (void *)0;
    kmsg->ikm_n_ports = 0;
    kmsg->ikm_n_ool   = 0;
    kmemcpy(kmsg->ikm_data, msg, size);

    /*
     * Step 5: Process typed descriptors for complex messages.
     *
     * Port rights are extracted from the sender's space and stored in
     * ikm_ports[].  OOL buffers are copied into kernel memory and stored
     * in ikm_ool[].  The corresponding fields in ikm_data are zeroed so
     * the receive side can fill them in with receiver-space values.
     */
    if (((mach_msg_header_t *)kmsg->ikm_data)->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
        kern_return_t kr = process_send_descriptors(sender, kmsg);
        if (kr != KERN_SUCCESS) {
            /* Free any OOL buffers already allocated */
            for (uint8_t j = 0; j < kmsg->ikm_n_ool; j++) {
                if (kmsg->ikm_ool[j].buf)
                    kfree(kmsg->ikm_ool[j].buf);
            }
            kfree(kmsg);
            return kr;
        }
    }

    /*
     * Step 6: Enqueue the kmsg on the port's message queue.
     */
    struct ipc_mqueue *mq = port->ip_messages;
    if (!mq) {
        for (uint8_t j = 0; j < kmsg->ikm_n_ool; j++) {
            if (kmsg->ikm_ool[j].buf)
                kfree(kmsg->ikm_ool[j].buf);
        }
        kfree(kmsg);
        return KERN_FAILURE;
    }

    mach_msg_return_t mr = ipc_mqueue_enqueue(mq, kmsg);
    if (mr != MACH_MSG_SUCCESS) {
        for (uint8_t j = 0; j < kmsg->ikm_n_ool; j++) {
            if (kmsg->ikm_ool[j].buf)
                kfree(kmsg->ikm_ool[j].buf);
        }
        kfree(kmsg);
        return KERN_RESOURCE_SHORTAGE;
    }

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * mach_msg_receive_core — internal helper shared by receive variants
 * ------------------------------------------------------------------------- */

static kern_return_t
mach_msg_receive_core(struct task *receiver,
                       mach_port_name_t port_name,
                       void *buf,
                       mach_msg_size_t buf_size,
                       mach_msg_size_t *out_size,
                       mach_msg_timeout_t timeout_ms)
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
     * Dequeue a message, blocking until timeout_ms expires if the queue
     * is empty.
     */
    struct ipc_kmsg *kmsg = ipc_mqueue_dequeue_timeout(port->ip_messages,
                                                        timeout_ms);

    if (!kmsg)
        return (timeout_ms != MACH_MSG_TIMEOUT_NONE)
               ? KERN_OPERATION_TIMED_OUT
               : KERN_FAILURE;

    /*
     * Process port and OOL descriptors in complex messages.
     * Port rights are installed in the receiver's ipc_space;
     * OOL buffer addresses are written into the message body.
     */
    if (((mach_msg_header_t *)kmsg->ikm_data)->msgh_bits & MACH_MSGH_BITS_COMPLEX) {
        (void)process_recv_descriptors(receiver, kmsg);
    }

    /* Copy message to caller buffer */
    mach_msg_size_t copy_size = kmsg->ikm_size;
    if (copy_size > buf_size)
        copy_size = buf_size;

    kmemcpy(buf, kmsg->ikm_data, copy_size);

    if (out_size)
        *out_size = kmsg->ikm_size;

    /* Free the kernel message (OOL buffers already transferred to receiver) */
    kfree(kmsg);

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * mach_msg_receive — non-blocking receive (Phase 1 compatible)
 * ------------------------------------------------------------------------- */

kern_return_t mach_msg_receive(struct task *receiver,
                               mach_port_name_t port_name,
                               void *buf,
                               mach_msg_size_t buf_size,
                               mach_msg_size_t *out_size)
{
    return mach_msg_receive_core(receiver, port_name, buf, buf_size,
                                  out_size, MACH_MSG_TIMEOUT_NONE);
}

/* -------------------------------------------------------------------------
 * mach_msg_receive_timeout — blocking receive with timeout (Phase 2)
 * ------------------------------------------------------------------------- */

kern_return_t mach_msg_receive_timeout(struct task *receiver,
                                        mach_port_name_t port_name,
                                        void *buf,
                                        mach_msg_size_t buf_size,
                                        mach_msg_size_t *out_size,
                                        mach_msg_timeout_t timeout_ms)
{
    return mach_msg_receive_core(receiver, port_name, buf, buf_size,
                                  out_size, timeout_ms);
}
