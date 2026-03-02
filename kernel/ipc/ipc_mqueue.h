/*
 * kernel/ipc/ipc_mqueue.h — Per-port message queue for UNHOX
 *
 * The message queue sits inside each ipc_port and holds incoming messages
 * for the port's receiver to consume.  In CMU Mach, the message queue is
 * the central data structure enabling asynchronous IPC: senders enqueue
 * messages without waiting for the receiver, and receivers dequeue when ready.
 *
 * Phase 2 adds blocking IPC:
 *   - Receivers block (sleep) if the queue is empty; the scheduler wakes
 *     them when a message arrives.
 *   - The spinlock is interrupt-safe (irq_save/irq_restore) to prevent
 *     deadlock under preemptive scheduling.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Messages;
 *            OSF MK ipc/ipc_mqueue.h for the original structure.
 */

#ifndef IPC_MQUEUE_H
#define IPC_MQUEUE_H

#include "mach/mach_types.h"
#include <stdint.h>
#include <stdatomic.h>

/* Forward declaration — threads waiting on this queue */
struct thread;

/*
 * Maximum number of messages queued on a single port before send fails.
 */
#define IPC_MQUEUE_MAX_DEPTH    16

/*
 * Maximum message size (header + body) in bytes.
 */
#define IPC_MQUEUE_MAX_MSG_SIZE 1024

/*
 * struct ipc_kmsg — a kernel-internal message buffer.
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
 * Threads waiting for a message form a linked list via th_wait_next.
 */
struct ipc_mqueue {
    struct ipc_kmsg    *imq_head;       /* first message in the queue        */
    struct ipc_kmsg    *imq_tail;       /* last message in the queue         */
    uint32_t            imq_count;      /* number of messages currently queued */
    uint32_t            imq_limit;      /* max messages before send fails    */
    atomic_flag         imq_lock;       /* spinlock protecting the queue     */

    /* Wait queue: threads blocked in ipc_mqueue_receive() */
    struct thread      *imq_wait_head;
    struct thread      *imq_wait_tail;
};

/* -------------------------------------------------------------------------
 * Public interface
 * ------------------------------------------------------------------------- */

void ipc_mqueue_init(struct ipc_mqueue *mq);

/*
 * ipc_mqueue_send — enqueue a message (copy semantics).
 * If a thread is blocked waiting on this queue, it is woken up.
 */
mach_msg_return_t ipc_mqueue_send(struct ipc_mqueue *mq,
                                   const void *msg,
                                   mach_msg_size_t msg_size);

/*
 * ipc_mqueue_receive — dequeue a message (copy semantics).
 *
 * If blocking is true and no message is available, the calling thread
 * sleeps until a message arrives.  If blocking is false, returns
 * MACH_RCV_TOO_LARGE immediately when the queue is empty.
 */
mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size,
                                      int blocking);

#endif /* IPC_MQUEUE_H */
