/*
 * kernel/ipc/mach_msg.h — Combined mach_msg() trap for UNHOX
 *
 * Declares the mach_msg_trap() entry point and the option flags.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.4 — mach_msg();
 *            Liedtke, "On µ-Kernel Construction" (SOSP 1995) §3 — IPC.
 */

#ifndef MACH_MSG_H
#define MACH_MSG_H

#include "mach/mach_types.h"

/* Forward declaration */
struct task;

/* -------------------------------------------------------------------------
 * mach_msg option flags
 *
 * These are OR'd together to select the send/receive/both modes.
 *
 * CMU Mach 3.0 paper §3.4: mach_msg() takes an option parameter that
 * controls whether it sends, receives, or both.
 * ------------------------------------------------------------------------- */
typedef uint32_t mach_msg_option_t;

#define MACH_MSG_OPTION_NONE    ((mach_msg_option_t) 0x00000000)
#define MACH_SEND_MSG           ((mach_msg_option_t) 0x00000001)
#define MACH_RCV_MSG            ((mach_msg_option_t) 0x00000002)
#define MACH_RCV_TIMEOUT        ((mach_msg_option_t) 0x00000100)
#define MACH_SEND_TIMEOUT       ((mach_msg_option_t) 0x00000200)
#define MACH_SEND_INTERRUPT     ((mach_msg_option_t) 0x00000400)
#define MACH_RCV_INTERRUPT      ((mach_msg_option_t) 0x00000800)

/*
 * mach_msg_trap — kernel entry point for the mach_msg() system call.
 *
 * option:        MACH_SEND_MSG | MACH_RCV_MSG (or just one)
 * msg:           the message buffer (send header on input, receive buffer on output)
 * send_size:     size of the message to send (0 if not sending)
 * rcv_size:      size of the receive buffer (0 if not receiving)
 * rcv_name:      port name to receive on (MACH_PORT_NULL if not receiving)
 * out_rcv_size:  OUT — actual received message size (NULL if not receiving)
 *
 * Returns KERN_SUCCESS or an error code.
 *
 * When MACH_SEND_MSG | MACH_RCV_MSG are both set, this implements the
 * L4-inspired combined send+receive (IPC call / RPC) in one syscall.
 */
kern_return_t mach_msg_trap(struct task *task,
                             mach_msg_option_t option,
                             mach_msg_header_t *msg,
                             mach_msg_size_t send_size,
                             mach_msg_size_t rcv_size,
                             mach_port_name_t rcv_name,
                             mach_msg_size_t *out_rcv_size);

/*
 * mach_msg_rpc — synchronous send+receive RPC helper.
 *
 * Sends a request to the port in request->msgh_remote_port and then issues
 * a receive on reply_port for the reply.
 *
 * Phase 1 semantics: the underlying mach_msg_receive() is non-blocking, so
 * mach_msg_rpc returns immediately with an error if no reply is queued on
 * reply_port.
 *
 * Planned Phase 2 semantics: mach_msg_receive() (and thus mach_msg_rpc) will
 * block waiting for a reply, matching traditional blocking RPC behavior.
 *
 * This follows the L4 "IPC call" pattern: one kernel entry for a full
 * round-trip, once full blocking semantics are implemented.
 */
kern_return_t mach_msg_rpc(struct task *task,
                            mach_msg_header_t *request,
                            mach_msg_size_t req_size,
                            mach_msg_header_t *reply_buf,
                            mach_msg_size_t reply_size,
                            mach_port_name_t reply_port,
                            mach_msg_size_t *out_size);

#endif /* MACH_MSG_H */
