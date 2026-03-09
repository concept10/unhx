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
 * descriptors.  Phase 1 limits inline messages to 4096 bytes.
 */
#define IPC_MQUEUE_MAX_MSG_SIZE 4096

/*
 * Maximum number of port-right descriptors carried in a single message.
 * Phase 2: limited to IPC_KMSG_MAX_PORTS; Phase 3+ will use a zone allocator.
 */
#define IPC_KMSG_MAX_PORTS  8

/*
 * Maximum number of out-of-line memory descriptors per message.
 */
#define IPC_KMSG_MAX_OOL    4

/* Forward declaration */
struct ipc_port;

/*
 * struct ikm_port_entry — one port right carried in a kernel message.
 *
 * Populated during mach_msg_send when a mach_msg_port_descriptor_t is
 * encountered in a complex message.  The receiver reads this table to
 * install the rights in its own ipc_space.
 */
struct ikm_port_entry {
    struct ipc_port    *port;           /* kernel port pointer               */
    mach_msg_type_name_t disposition;   /* MACH_MSG_TYPE_MOVE_SEND etc.      */
};

/*
 * struct ikm_ool_entry — one OOL buffer carried in a kernel message.
 *
 * Populated during mach_msg_send when a mach_msg_ool_descriptor_t is
 * encountered.  'buf' points to a kernel-allocated copy of the sender's
 * OOL data; it is freed after the receiver copies it out.
 */
struct ikm_ool_entry {
    void               *buf;            /* kalloc'd kernel buffer            */
    mach_msg_size_t     size;           /* buffer size in bytes              */
};

/*
 * struct ipc_kmsg — a kernel-internal message buffer.
 *
 * This is the in-kernel representation of a Mach message.  The actual
 * message data (header + payload) is stored in ikm_data[].
 *
 * Phase 2 additions:
 *   ikm_ports[] — port rights extracted from typed descriptors at send time;
 *                 installed into the receiver's ipc_space at receive time.
 *   ikm_ool[]   — OOL buffers copied at send time; delivered to receiver.
 *
 * In CMU Mach, ipc_kmsg is allocated from a zone allocator.
 * Phase 2 still uses kalloc; a slab allocator is a Phase 3 item.
 */
struct ipc_kmsg {
    struct ipc_kmsg    *ikm_next;       /* linked list linkage               */
    mach_msg_size_t     ikm_size;       /* total size of the message         */
    uint8_t             ikm_data[IPC_MQUEUE_MAX_MSG_SIZE]; /* message bytes  */

    /* Phase 2: port rights carried in message */
    uint8_t             ikm_n_ports;
    struct ikm_port_entry ikm_ports[IPC_KMSG_MAX_PORTS];

    /* Phase 2: OOL memory buffers */
    uint8_t             ikm_n_ool;
    struct ikm_ool_entry ikm_ool[IPC_KMSG_MAX_OOL];
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
 * ipc_mqueue_receive — dequeue a message (copy semantics), non-blocking.
 *
 * Copies the oldest message into buf (up to buf_size bytes).
 * The ipc_kmsg is freed after copying.
 *
 * On success, *out_size is set to the actual message size.
 *
 * Returns:
 *   MACH_MSG_SUCCESS    — message dequeued successfully
 *   MACH_RCV_TOO_LARGE  — no message available (queue empty)
 *
 * NOTE: Does NOT process port or OOL descriptors.  For complex messages
 * use ipc_mqueue_dequeue() and handle descriptors in mach_msg_receive().
 */
mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size);

/* -------------------------------------------------------------------------
 * Phase 2 additions
 * ------------------------------------------------------------------------- */

/*
 * ipc_mqueue_dequeue — dequeue the oldest message without copying or freeing.
 *
 * Returns the ipc_kmsg pointer, or NULL if the queue is empty.
 * The caller is responsible for processing the message and calling kfree().
 *
 * Use this for complex messages (port rights, OOL buffers) where the caller
 * needs to inspect ikm_ports[] and ikm_ool[] before freeing.
 */
struct ipc_kmsg *ipc_mqueue_dequeue(struct ipc_mqueue *mq);

/*
 * ipc_mqueue_dequeue_timeout — dequeue a message with a blocking timeout.
 *
 * Spins until a message is available or the timeout expires.
 *
 * timeout_ms: milliseconds to wait (MACH_MSG_TIMEOUT_NONE = 0 means
 *             non-blocking — equivalent to ipc_mqueue_dequeue).
 *
 * Returns the ipc_kmsg pointer on success, or NULL on timeout/empty.
 *
 * Note: In Phase 2 this is a busy-wait loop; Phase 3+ will use the
 * scheduler's thread sleep/wakeup path once timer interrupts are available.
 */
struct ipc_kmsg *ipc_mqueue_dequeue_timeout(struct ipc_mqueue *mq,
                                             uint32_t timeout_ms);

/*
 * ipc_mqueue_enqueue — enqueue a pre-allocated ipc_kmsg (Phase 2).
 *
 * Used by mach_msg_send() after populating ikm_ports[] and ikm_ool[],
 * allowing the send path to allocate and fill the kmsg once without an
 * extra copy.
 *
 * The mq takes ownership of kmsg on MACH_MSG_SUCCESS.
 * On failure the caller retains ownership and must free kmsg.
 */
mach_msg_return_t ipc_mqueue_enqueue(struct ipc_mqueue *mq,
                                      struct ipc_kmsg *kmsg);

/*
 * ipc_mqueue_drain — free all messages currently queued on the port.
 *
 * Also frees any OOL buffers stored in each ipc_kmsg.
 * Called from ipc_port_destroy() to clean up before releasing the port.
 */
void ipc_mqueue_drain(struct ipc_mqueue *mq);

#endif /* IPC_MQUEUE_H */
