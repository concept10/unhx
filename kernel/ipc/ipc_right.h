/*
 * kernel/ipc/ipc_right.h — Mach port right management for UNHOX
 *
 * Declares the public interface for port right lifecycle operations:
 * allocation, copying, transferring, and deallocating port rights.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.3 — Port Rights;
 *            OSF MK ipc/ipc_right.h.
 */

#ifndef IPC_RIGHT_H
#define IPC_RIGHT_H

#include "mach/mach_types.h"
#include "ipc_port.h"
#include "ipc_space.h"
#include "ipc_entry.h"

/* Forward declaration */
struct task;

/*
 * ipc_right_alloc_receive — create a new port with a RECEIVE right in task.
 *
 * task:       the task that will own the receive right
 * namep:      OUT — the port name assigned in task's space
 * portp:      OUT (optional) — the kernel port pointer
 * also_send:  if non-zero, also install a SEND right in the same entry
 *             (ie_bits = RECEIVE | SEND, urefs = 1)
 *
 * Returns KERN_SUCCESS or an error.
 */
kern_return_t ipc_right_alloc_receive(struct task *task,
                                      mach_port_name_t *namep,
                                      struct ipc_port **portp,
                                      int also_send);

/*
 * ipc_right_copy_send — copy a SEND right from src_task into dst_task.
 *
 * The source right is retained (not consumed).
 * Increments ip_send_rights on the port.
 *
 * Returns KERN_SUCCESS or an error.
 */
kern_return_t ipc_right_copy_send(struct task *src_task,
                                  mach_port_name_t src_name,
                                  struct task *dst_task,
                                  mach_port_name_t *dst_namep);

/*
 * ipc_right_make_send_once — create a SEND_ONCE right in dst_task.
 *
 * task:      the task holding the RECEIVE right (required to make send-once)
 * port_name: the port name in task's space (must hold RECEIVE right)
 * dst_task:  the task that will receive the SEND_ONCE right
 * dst_namep: OUT — the port name in dst_task's space
 *
 * Returns KERN_SUCCESS or an error.
 */
kern_return_t ipc_right_make_send_once(struct task *task,
                                       mach_port_name_t port_name,
                                       struct task *dst_task,
                                       mach_port_name_t *dst_namep);

/*
 * ipc_right_deallocate — release one user reference to a right.
 *
 * For SEND rights: decrements urefs; removes entry when urefs reaches 0.
 * For RECEIVE rights: destroys the port (all send rights become dead names).
 * For SEND_ONCE rights: removes the entry unconditionally.
 *
 * Returns KERN_SUCCESS or an error.
 */
kern_return_t ipc_right_deallocate(struct task *task, mach_port_name_t name);

/*
 * ipc_right_transfer — move a right from src_task to dst_task.
 *
 * The right is removed from src_task's space and installed in dst_task's.
 * For RECEIVE rights, the port's ip_receiver is updated.
 *
 * Returns KERN_SUCCESS or an error.
 */
kern_return_t ipc_right_transfer(struct task *src_task,
                                 mach_port_name_t src_name,
                                 struct task *dst_task,
                                 mach_port_name_t *dst_namep);

#endif /* IPC_RIGHT_H */
