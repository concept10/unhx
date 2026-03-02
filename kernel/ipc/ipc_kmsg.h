/*
 * kernel/ipc/ipc_kmsg.h — mach_msg() kernel entry point for UNHOX
 *
 * mach_msg() is the single most important system call in Mach.
 * ALL inter-process communication goes through it.
 *
 * In the full Mach design, mach_msg() supports:
 *   - Send only (MACH_SEND_MSG)
 *   - Receive only (MACH_RCV_MSG)
 *   - Send + Receive combined (the RPC pattern — sends a request and blocks
 *     for the reply in one system call, avoiding two context switches)
 *
 * Phase 1 implements send and receive as separate operations.
 * Combined send+receive and blocking semantics are deferred to Phase 2.
 *
 * SECURITY INVARIANT:
 * A task can only send to ports for which it holds a send right.
 * This is the capability model — port names are unforgeable within a task's
 * space.  The kernel looks up the destination port name in the sender's
 * ipc_space; if no send right is found, the operation fails.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC.
 *
 * TODO: Out-of-line memory descriptors (mapping VM regions into receiver's
 *       space).
 * TODO: Port rights carried in messages (transferring capabilities between
 *       tasks).
 * TODO: Blocking semantics (thread sleep/wakeup on send/receive).
 * TODO: Combined send+receive for RPC pattern.
 */

#ifndef IPC_KMSG_H
#define IPC_KMSG_H

#include "mach/mach_types.h"

/* Forward declarations */
struct task;

/*
 * mach_msg_send — send a message from a task.
 *
 * sender: the task initiating the send
 * msg:    pointer to the message (header + payload)
 * size:   total message size in bytes
 *
 * Security: verifies the sender holds a SEND right to the destination port
 * (msgh_remote_port in the message header).
 *
 * Returns:
 *   KERN_SUCCESS           — message sent successfully
 *   KERN_INVALID_ARGUMENT  — msg is NULL or size too small
 *   KERN_INVALID_NAME      — destination port not in sender's space
 *   KERN_INVALID_RIGHT     — sender does not hold SEND right
 *   KERN_FAILURE           — enqueue failed (queue full or port dead)
 */
kern_return_t mach_msg_send(struct task *sender,
                            mach_msg_header_t *msg,
                            mach_msg_size_t size);

/*
 * mach_msg_receive — receive a message on a port owned by a task.
 *
 * receiver: the task receiving the message
 * port_name: port name in the receiver's space (must hold RECEIVE right)
 * buf:       buffer to copy the message into
 * buf_size:  size of the buffer
 * out_size:  receives the actual message size
 *
 * Returns:
 *   KERN_SUCCESS           — message received successfully
 *   KERN_INVALID_NAME      — port_name not in receiver's space
 *   KERN_NOT_RECEIVER      — task does not hold RECEIVE right for this port
 *   KERN_FAILURE           — no message available (non-blocking Phase 1)
 */
kern_return_t mach_msg_receive(struct task *receiver,
                               mach_port_name_t port_name,
                               void *buf,
                               mach_msg_size_t buf_size,
                               mach_msg_size_t *out_size);

#endif /* IPC_KMSG_H */
