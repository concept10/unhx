/*
 * kernel/ipc/ipc_mqueue.c — Per-port message queue for UNHOX
 *
 * See ipc_mqueue.h for design rationale and Phase 2 TODO markers.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Messages.
 */

#include "ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"

/* Spinlock helpers */
static inline void mqueue_lock(struct ipc_mqueue *mq)
{
    while (atomic_flag_test_and_set_explicit(&mq->imq_lock,
                                              memory_order_acquire))
        ; /* spin */
}

static inline void mqueue_unlock(struct ipc_mqueue *mq)
{
    atomic_flag_clear_explicit(&mq->imq_lock, memory_order_release);
}

void ipc_mqueue_init(struct ipc_mqueue *mq)
{
    mq->imq_head  = (void *)0;
    mq->imq_tail  = (void *)0;
    mq->imq_count = 0;
    mq->imq_limit = IPC_MQUEUE_MAX_DEPTH;
    mq->imq_lock  = (atomic_flag)ATOMIC_FLAG_INIT;
}

mach_msg_return_t ipc_mqueue_send(struct ipc_mqueue *mq,
                                   const void *msg,
                                   mach_msg_size_t msg_size)
{
    /* Validate message size */
    if (msg_size < sizeof(mach_msg_header_t))
        return MACH_SEND_MSG_TOO_SMALL;

    if (msg_size > IPC_MQUEUE_MAX_MSG_SIZE)
        return MACH_SEND_TOO_LARGE;

    mqueue_lock(mq);

    /* Check queue depth limit */
    if (mq->imq_count >= mq->imq_limit) {
        mqueue_unlock(mq);
        return MACH_SEND_NO_BUFFER;
    }

    /* Allocate a kernel message buffer */
    struct ipc_kmsg *kmsg = (struct ipc_kmsg *)kalloc(sizeof(struct ipc_kmsg));
    if (!kmsg) {
        mqueue_unlock(mq);
        return MACH_SEND_NO_BUFFER;
    }

    /* Copy the message data */
    kmsg->ikm_size = msg_size;
    kmsg->ikm_next = (void *)0;
    kmemcpy(kmsg->ikm_data, msg, msg_size);

    /* Enqueue at tail (FIFO) */
    if (mq->imq_tail) {
        mq->imq_tail->ikm_next = kmsg;
    } else {
        mq->imq_head = kmsg;
    }
    mq->imq_tail = kmsg;
    mq->imq_count++;

    mqueue_unlock(mq);
    return MACH_MSG_SUCCESS;
}

mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size)
{
    mqueue_lock(mq);

    if (!mq->imq_head) {
        mqueue_unlock(mq);
        /*
         * No message available.  In real Mach, the thread would block here
         * and be woken when a message arrives.  Phase 1 returns an error.
         *
         * TODO (Phase 2): Implement blocking receive with thread wakeup.
         *                 The scheduler will need a wait queue per mqueue.
         */
        return MACH_RCV_TOO_LARGE;
    }

    /* Dequeue from head (FIFO) */
    struct ipc_kmsg *kmsg = mq->imq_head;
    mq->imq_head = kmsg->ikm_next;
    if (!mq->imq_head)
        mq->imq_tail = (void *)0;
    mq->imq_count--;

    mqueue_unlock(mq);

    /* Copy message to caller buffer */
    mach_msg_size_t copy_size = kmsg->ikm_size;
    if (copy_size > buf_size)
        copy_size = buf_size;

    kmemcpy(buf, kmsg->ikm_data, copy_size);

    if (out_size)
        *out_size = kmsg->ikm_size;

    /* Free the kernel message buffer */
    kfree(kmsg);

    return MACH_MSG_SUCCESS;
}
