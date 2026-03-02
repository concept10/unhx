/*
 * kernel/ipc/ipc_mqueue.c — Per-port message queue for UNHOX
 *
 * Phase 2: blocking IPC.  Receivers sleep if the queue is empty and are
 * woken by senders when a message arrives.  The spinlock is interrupt-safe
 * (irq_save/irq_restore) to prevent deadlock under preemptive scheduling.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Messages.
 */

#include "ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "kern/sched.h"
#include "platform/irq.h"

/* Interrupt-safe spinlock helpers */
static inline uint64_t mqueue_lock(struct ipc_mqueue *mq)
{
    uint64_t flags = irq_save();
    while (atomic_flag_test_and_set_explicit(&mq->imq_lock,
                                              memory_order_acquire))
        ; /* spin */
    return flags;
}

static inline void mqueue_unlock(struct ipc_mqueue *mq, uint64_t flags)
{
    atomic_flag_clear_explicit(&mq->imq_lock, memory_order_release);
    irq_restore(flags);
}

void ipc_mqueue_init(struct ipc_mqueue *mq)
{
    mq->imq_head  = (void *)0;
    mq->imq_tail  = (void *)0;
    mq->imq_count = 0;
    mq->imq_limit = IPC_MQUEUE_MAX_DEPTH;
    mq->imq_lock  = (atomic_flag)ATOMIC_FLAG_INIT;
    mq->imq_wait_head = (void *)0;
    mq->imq_wait_tail = (void *)0;
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

    uint64_t flags = mqueue_lock(mq);

    /* Check queue depth limit */
    if (mq->imq_count >= mq->imq_limit) {
        mqueue_unlock(mq, flags);
        return MACH_SEND_NO_BUFFER;
    }

    /* Allocate a kernel message buffer */
    struct ipc_kmsg *kmsg = (struct ipc_kmsg *)kalloc(sizeof(struct ipc_kmsg));
    if (!kmsg) {
        mqueue_unlock(mq, flags);
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

    /* Wake the first thread waiting on this queue, if any */
    struct thread *waiter = mq->imq_wait_head;
    if (waiter) {
        mq->imq_wait_head = waiter->th_wait_next;
        if (!mq->imq_wait_head)
            mq->imq_wait_tail = (void *)0;
        waiter->th_wait_next = (void *)0;
    }

    mqueue_unlock(mq, flags);

    if (waiter)
        sched_wakeup(waiter);

    return MACH_MSG_SUCCESS;
}

mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size,
                                      int blocking)
{
retry:
    ;  /* label requires a statement */
    uint64_t flags = mqueue_lock(mq);

    if (mq->imq_head) {
        /* Message available — dequeue from head (FIFO) */
        struct ipc_kmsg *kmsg = mq->imq_head;
        mq->imq_head = kmsg->ikm_next;
        if (!mq->imq_head)
            mq->imq_tail = (void *)0;
        mq->imq_count--;

        mqueue_unlock(mq, flags);

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

    /* Queue is empty */
    if (!blocking) {
        mqueue_unlock(mq, flags);
        return MACH_RCV_TOO_LARGE;
    }

    /*
     * Block the current thread on this queue's wait list.
     * We add ourselves to the tail, unlock the mqueue (so senders can
     * enqueue and wake us), then call sched_sleep() which sets our state
     * to WAITING and yields the CPU.
     *
     * On wakeup (triggered by ipc_mqueue_send), we loop back to retry
     * the dequeue.
     */
    struct thread *cur = sched_current();
    cur->th_wait_next = (void *)0;
    if (mq->imq_wait_tail) {
        mq->imq_wait_tail->th_wait_next = cur;
    } else {
        mq->imq_wait_head = cur;
    }
    mq->imq_wait_tail = cur;

    mqueue_unlock(mq, flags);

    sched_sleep();

    goto retry;
}
