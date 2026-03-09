/*
 * kernel/ipc/mach_msg.c — Combined mach_msg() trap for NEOMACH
 *
 * mach_msg() is the single most performance-critical kernel entry point in
 * the Mach design.  ALL inter-process communication goes through it.
 *
 * This file implements the combined mach_msg() trap that supports three
 * operation modes:
 *
 *   MACH_SEND_MSG only      — send a message and return
 *   MACH_RCV_MSG only       — receive a message and return
 *   MACH_SEND_MSG | MACH_RCV_MSG — send then immediately block for reply
 *                             (the RPC pattern)
 *
 * =========================================================================
 * L4-INSPIRED PERFORMANCE DESIGN
 * =========================================================================
 *
 * CMU Mach's original IPC suffered from high per-message overhead due to:
 *   1. Too many kernel/user crossings
 *   2. Complex message type system with many slow paths
 *   3. Unnecessary copying (even for small messages)
 *   4. No combined send+receive (2 syscalls per RPC = 2x overhead)
 *
 * L4 (Liedtke, 1993 onward) proved that microkernel IPC could be extremely
 * fast by:
 *   a) Minimising the kernel path for the common case (small message, one hop)
 *   b) Combining send+receive into a single syscall (avoiding a round-trip)
 *   c) Using registers instead of memory for small messages (avoiding copies)
 *   d) Making the IPC path auditable and cache-friendly
 *
 * NEOMACH Phase 1 adopts the key L4 principle of the combined send+receive
 * operation.  For a two-task RPC:
 *
 *   Client: mach_msg(SEND|RCV, request, reply_port)
 *     → kernel delivers request to server
 *     → kernel blocks client waiting for reply on reply_port
 *   Server: mach_msg(RCV, port)   — receives request
 *           [process request]
 *           mach_msg(SEND, reply)  — sends reply
 *     → kernel wakes client with reply
 *
 * With the combined operation, the client issues ONE syscall; without it,
 * it would need two (send + receive), doubling the syscall overhead.
 *
 * Phase 1 limitation: Blocking semantics require a thread scheduler with
 * sleep/wakeup.  The combined path falls back to non-blocking in Phase 1.
 * Full blocking is marked TODO and scheduled for Phase 2.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.4 — mach_msg();
 *            Liedtke, "On µ-Kernel Construction" (SOSP 1995) §3 — IPC;
 *            L4 x2 ABI reference §2 — IPC system call.
 *
 * See docs/research/ipc-performance.md for performance analysis and
 * benchmarking methodology.
 */

#include "mach_msg.h"
#include "ipc_kmsg.h"
#include "ipc.h"
#include "ipc_right.h"
#include "kern/task.h"

/* -------------------------------------------------------------------------
 * mach_msg_trap
 *
 * The kernel entry point called when a task invokes mach_msg().
 *
 * option flags:
 *   MACH_SEND_MSG (bit 0) — send msg to msgh_remote_port
 *   MACH_RCV_MSG  (bit 1) — receive into msg from rcv_name port
 *
 * When both flags are set (the RPC pattern):
 *   1. Send the message (Phase 1: non-blocking, returns error if queue full)
 *   2. Immediately attempt to receive on rcv_name
 *      Phase 1: non-blocking — returns KERN_FAILURE if no reply yet.
 *      Phase 2: will block the calling thread until a reply arrives.
 *
 * This combined path is the L4-inspired optimisation.  Even in Phase 1's
 * non-blocking form it eliminates the need for a separate send + separate
 * receive in the common RPC case where the server replies synchronously
 * before this call returns.
 * ------------------------------------------------------------------------- */
kern_return_t
mach_msg_trap(struct task *task,
              mach_msg_option_t option,
              mach_msg_header_t *msg,
              mach_msg_size_t send_size,
              mach_msg_size_t rcv_size,
              mach_port_name_t rcv_name,
              mach_msg_size_t *out_rcv_size,
              mach_msg_timeout_t timeout)
{
    if (!task)
        return KERN_INVALID_ARGUMENT;

    kern_return_t kr = KERN_SUCCESS;

    /* ------------------------------------------------------------------ */
    /* SEND phase                                                           */
    /* ------------------------------------------------------------------ */
    if (option & MACH_SEND_MSG) {
        if (!msg)
            return KERN_INVALID_ARGUMENT;

        if (send_size < sizeof(mach_msg_header_t))
            return KERN_INVALID_ARGUMENT;

        kr = mach_msg_send(task, msg, send_size);
        if (kr != KERN_SUCCESS)
            return kr;
    }

    /* ------------------------------------------------------------------ */
    /* RECEIVE phase                                                        */
    /* ------------------------------------------------------------------ */
    if (option & MACH_RCV_MSG) {
        if (!msg)
            return KERN_INVALID_ARGUMENT;

        if (rcv_name == MACH_PORT_NULL)
            return KERN_INVALID_NAME;

        mach_msg_size_t actual_rcv_size = 0;

        /*
         * Phase 2: honour MACH_RCV_TIMEOUT.
         * When MACH_RCV_TIMEOUT is set, pass the timeout value through to
         * mach_msg_receive_timeout() so the receive blocks until either a
         * message arrives or the timeout expires.
         *
         * When MACH_RCV_TIMEOUT is NOT set, use MACH_MSG_TIMEOUT_NONE which
         * makes the receive non-blocking (returns immediately if empty).
         *
         * TODO (Phase 3): Replace the busy-wait loop in
         * mach_msg_receive_timeout() with a scheduler sleep/wakeup once
         * preemptive scheduling and APIC timer interrupts are available.
         */
        mach_msg_timeout_t rcv_timeout =
            (option & MACH_RCV_TIMEOUT) ? timeout : MACH_MSG_TIMEOUT_NONE;

        kr = mach_msg_receive_timeout(task, rcv_name,
                                       msg, rcv_size,
                                       &actual_rcv_size,
                                       rcv_timeout);

        if (out_rcv_size)
            *out_rcv_size = actual_rcv_size;

        if (kr != KERN_SUCCESS)
            return kr;
    }

    return KERN_SUCCESS;
}

/* -------------------------------------------------------------------------
 * mach_msg_rpc
 *
 * Convenience wrapper implementing the send-then-receive RPC pattern
 * explicitly.  This is the most common IPC operation in a Mach system:
 * a client sends a request and waits for a reply.
 *
 * L4 terminology: this is an "IPC call" — send + receive in one operation.
 *
 * task:        the calling task
 * request:     the request message to send (header contains remote port)
 * req_size:    size of the request message
 * reply_buf:   buffer to receive the reply into
 * reply_size:  size of the reply buffer
 * reply_port:  port name in task's space to receive on (must hold RECEIVE)
 * out_size:    OUT — actual size of received reply
 * ------------------------------------------------------------------------- */
kern_return_t
mach_msg_rpc(struct task *task,
             mach_msg_header_t *request,
             mach_msg_size_t req_size,
             mach_msg_header_t *reply_buf,
             mach_msg_size_t reply_size,
             mach_port_name_t reply_port,
             mach_msg_size_t *out_size)
{
    if (!task || !request || !reply_buf)
        return KERN_INVALID_ARGUMENT;

    /* Send the request */
    kern_return_t kr = mach_msg_send(task, request, req_size);
    if (kr != KERN_SUCCESS)
        return kr;

    /* Receive the reply */
    return mach_msg_receive(task, reply_port, reply_buf, reply_size, out_size);
}
