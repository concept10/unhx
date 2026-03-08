/*
 * kernel/ipc/ipc_mqueue.h — Per-port message queue for NEOMACH
 *
 * The message queue sits inside each ipc_port and holds incoming messages
 * for the port's receiver to consume.  In CMU Mach, the message queue is
 * the central data structure enabling asynchronous IPC: senders enqueue
 * messages without waiting for the receiver, and receivers dequeue when ready.
 *
 * Phase 1 implementation:
 *   - Non-blocking only (no thread sleep/wakeup).
 *   - Send returns MACH_SEND_NO_BUFFER if the queue is full.
 *   - Receive returns MACH_RCV_TOO_LARGE if the queue is empty (misnamed
 *     for historical compatibility — really means "no message available").
 *   - Copy semantics: messages are copied into kernel-allocated buffers.
 *
 * TODO (Phase 2): Real Mach uses a more complex scheme where:
 *   - Senders block if the queue is full (with a configurable queue limit).
 *   - Receivers block if the queue is empty, and the scheduler wakes them
 *     when a message arrives.
 *   - The send+receive combined operation (RPC pattern) is optimised to
 *     avoid extra context switches (handoff scheduling).
 *   These features require the thread scheduler, deferred to Phase 2.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Messages;
 *            OSF MK ipc/ipc_mqueue.h for the original structure.
 */

#ifndef IPC_MQUEUE_H
#define IPC_MQUEUE_H

#include "mach/mach_types.h"
#include <stdint.h>
#include <stdatomic.h>

/*
 * Maximum number of messages queued on a single port before send fails.
 * OSF MK uses a per-port configurable limit; we use a fixed global limit
 * for Phase 1 simplicity.
 */
#define IPC_MQUEUE_MAX_DEPTH    16

/*
 * Maximum message size (header + body) in bytes.
 * Real Mach supports arbitrarily large messages via out-of-line memory
 * descriptors.  Phase 1 limits inline messages to 1024 bytes.
 */
#define IPC_MQUEUE_MAX_MSG_SIZE 1024

/*
 * struct ipc_kmsg — a kernel-internal message buffer.
 *
 * This is the in-kernel representation of a Mach message.  The actual
 * message data (header + payload) is stored in ikm_data[].
 *
 * In CMU Mach, ipc_kmsg is allocated from a zone allocator and can hold
 * arbitrarily large messages.  Phase 1 uses a fixed-size buffer.
 */
struct ipc_kmsg {
    struct ipc_kmsg    *ikm_next;       /* linked list linkage               */
    mach_msg_size_t     ikm_size;       /* total size of the message         */
    uint8_t             ikm_data[IPC_MQUEUE_MAX_MSG_SIZE]; /* message bytes  */
};

/*
 * struct ipc_mqueue — the message queue for one port.
 *
 * Messages are enqueued at the tail and dequeued from the head (FIFO).
 */
struct ipc_mqueue {
    struct ipc_kmsg    *imq_head;       /* first message in the queue        */
    struct ipc_kmsg    *imq_tail;       /* last message in the queue         */
    uint32_t            imq_count;      /* number of messages currently queued */
    uint32_t            imq_limit;      /* max messages before send fails    */
    atomic_flag         imq_lock;       /* spinlock protecting the queue     */
};

/* -------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

/*
 * ipc_mqueue_init — initialise an empty message queue.
 */
void ipc_mqueue_init(struct ipc_mqueue *mq);

/*
 * ipc_mqueue_send — enqueue a message (copy semantics).
 *
 * Copies msg_size bytes from msg into a new ipc_kmsg and appends it
 * to the queue.
 *
 * Returns:
 *   MACH_MSG_SUCCESS   — message enqueued successfully
 *   MACH_SEND_NO_BUFFER — queue is full (at imq_limit)
 *   MACH_SEND_MSG_TOO_SMALL — msg_size < sizeof(mach_msg_header_t)
 *   MACH_SEND_TOO_LARGE — msg_size > IPC_MQUEUE_MAX_MSG_SIZE
 */
mach_msg_return_t ipc_mqueue_send(struct ipc_mqueue *mq,
                                   const void *msg,
                                   mach_msg_size_t msg_size);

/*
 * ipc_mqueue_receive — dequeue a message (copy semantics).
 *
 * Copies the oldest message into buf (up to buf_size bytes).
 * The ipc_kmsg is freed after copying.
 *
 * On success, *out_size is set to the actual message size.
 *
 * Returns:
 *   MACH_MSG_SUCCESS    — message dequeued successfully
 *   MACH_RCV_TOO_LARGE  — no message available (queue empty)
 */
mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size);

#endif /* IPC_MQUEUE_H */
