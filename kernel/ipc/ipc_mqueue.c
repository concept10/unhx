/*
 * kernel/ipc/ipc_mqueue.c — Per-port message queue for NEOMACH
 *
 * See ipc_mqueue.h for design rationale and Phase 2 TODO markers.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3.2 — Messages.
 */

#include "ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"

/* -------------------------------------------------------------------------
 * Platform cycle counter for timeout implementation
 *
 * Phase 2 uses a busy-wait loop with a hardware cycle counter for timed
 * receives.  Phase 3+ will replace this with scheduler sleep/wakeup once
 * the preemptive scheduler and APIC timer are available.
 *
 * On x86-64: use rdtsc (constant TSC frequency on modern CPUs).
 * On AArch64: use CNTVCT_EL0 (virtual counter, always-on).
 * Elsewhere: fall back to a monotonic software counter (QEMU TCG etc.).
 * ------------------------------------------------------------------------- */
static inline uint64_t ipc_read_clock(void)
{
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    static uint64_t counter = 0;
    return ++counter;
#endif
}

/*
 * Approximate cycles per millisecond.
 *
 * We use a conservative estimate of 3 GHz (3 × 10^6 cycles/ms) for x86-64
 * and a typical AArch64 system counter frequency of 24 MHz (24 × 10^3
 * ticks/ms).  This does not need to be exact — it is only used for the
 * timeout busy-wait loop, where a slight over/under-count is acceptable.
 *
 * A proper implementation would calibrate against the PIT or HPET during
 * kernel init and store the frequency in a global variable.
 */
#if defined(__aarch64__)
#define IPC_CLOCK_TICKS_PER_MS  24000ULL   /* 24 MHz generic timer */
#else
#define IPC_CLOCK_TICKS_PER_MS  3000000ULL /* 3 GHz TSC estimate   */
#endif

/* -------------------------------------------------------------------------
 * Spinlock helpers
 * ------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * ipc_mqueue_init
 * ------------------------------------------------------------------------- */

void ipc_mqueue_init(struct ipc_mqueue *mq)
{
    mq->imq_head  = (void *)0;
    mq->imq_tail  = (void *)0;
    mq->imq_count = 0;
    mq->imq_limit = IPC_MQUEUE_MAX_DEPTH;
    mq->imq_lock  = (atomic_flag)ATOMIC_FLAG_INIT;
}

/* -------------------------------------------------------------------------
 * ipc_mqueue_send
 * ------------------------------------------------------------------------- */

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
    kmsg->ikm_size    = msg_size;
    kmsg->ikm_next    = (void *)0;
    kmsg->ikm_n_ports = 0;
    kmsg->ikm_n_ool   = 0;
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

/* -------------------------------------------------------------------------
 * ipc_mqueue_receive — non-blocking simple receive (Phase 1 compatible)
 * ------------------------------------------------------------------------- */

mach_msg_return_t ipc_mqueue_receive(struct ipc_mqueue *mq,
                                      void *buf,
                                      mach_msg_size_t buf_size,
                                      mach_msg_size_t *out_size)
{
    mqueue_lock(mq);

    if (!mq->imq_head) {
        mqueue_unlock(mq);
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

/* -------------------------------------------------------------------------
 * ipc_mqueue_dequeue — dequeue without copying or freeing (Phase 2)
 * ------------------------------------------------------------------------- */

struct ipc_kmsg *ipc_mqueue_dequeue(struct ipc_mqueue *mq)
{
    mqueue_lock(mq);

    if (!mq->imq_head) {
        mqueue_unlock(mq);
        return (void *)0;
    }

    struct ipc_kmsg *kmsg = mq->imq_head;
    mq->imq_head = kmsg->ikm_next;
    if (!mq->imq_head)
        mq->imq_tail = (void *)0;
    mq->imq_count--;
    kmsg->ikm_next = (void *)0;

    mqueue_unlock(mq);
    return kmsg;
}

/* -------------------------------------------------------------------------
 * ipc_mqueue_dequeue_timeout — dequeue with busy-wait timeout (Phase 2)
 *
 * When timeout_ms == MACH_MSG_TIMEOUT_NONE (0), behaves identically to
 * ipc_mqueue_dequeue() (non-blocking).
 *
 * When timeout_ms > 0, spins until a message arrives or the timeout
 * (in milliseconds) expires.
 *
 * TODO (Phase 3): Replace the spin loop with scheduler sleep/wakeup once
 * preemptive scheduling and APIC timer are available.
 * ------------------------------------------------------------------------- */

struct ipc_kmsg *ipc_mqueue_dequeue_timeout(struct ipc_mqueue *mq,
                                             uint32_t timeout_ms)
{
    /* Non-blocking fast path */
    if (timeout_ms == MACH_MSG_TIMEOUT_NONE)
        return ipc_mqueue_dequeue(mq);

    /* Compute absolute deadline */
    uint64_t deadline = ipc_read_clock() +
                        (uint64_t)timeout_ms * IPC_CLOCK_TICKS_PER_MS;

    for (;;) {
        struct ipc_kmsg *kmsg = ipc_mqueue_dequeue(mq);
        if (kmsg)
            return kmsg;

        /* Check timeout */
        if (ipc_read_clock() >= deadline)
            return (void *)0;   /* timed out */

        /* Brief pause to reduce bus contention on the spinlock */
#if defined(__x86_64__)
        __asm__ volatile("pause" ::: "memory");
#endif
    }
}

/* -------------------------------------------------------------------------
 * ipc_mqueue_enqueue — enqueue a pre-allocated kmsg (Phase 2)
 * ------------------------------------------------------------------------- */

mach_msg_return_t ipc_mqueue_enqueue(struct ipc_mqueue *mq,
                                      struct ipc_kmsg *kmsg)
{
    mqueue_lock(mq);

    if (mq->imq_count >= mq->imq_limit) {
        mqueue_unlock(mq);
        return MACH_SEND_NO_BUFFER;
    }

    kmsg->ikm_next = (void *)0;

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

/* -------------------------------------------------------------------------
 * ipc_mqueue_drain — free all queued messages (Phase 2)
 *
 * Called from ipc_port_destroy() before releasing the port object.
 * Also frees any OOL kernel buffers embedded in each ipc_kmsg.
 * ------------------------------------------------------------------------- */

void ipc_mqueue_drain(struct ipc_mqueue *mq)
{
    mqueue_lock(mq);

    struct ipc_kmsg *kmsg = mq->imq_head;
    mq->imq_head  = (void *)0;
    mq->imq_tail  = (void *)0;
    mq->imq_count = 0;

    mqueue_unlock(mq);

    /* Free each message and any associated OOL buffers */
    while (kmsg) {
        struct ipc_kmsg *next = kmsg->ikm_next;

        /* Free OOL buffers */
        for (uint8_t i = 0; i < kmsg->ikm_n_ool; i++) {
            if (kmsg->ikm_ool[i].buf)
                kfree(kmsg->ikm_ool[i].buf);
        }

        kfree(kmsg);
        kmsg = next;
    }
}
