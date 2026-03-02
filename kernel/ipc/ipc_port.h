/*
 * kernel/ipc/ipc_port.h — Kernel-internal port object for UNHOX
 *
 * This header defines struct ipc_port, the kernel-side representation of a
 * Mach port.  Userspace tasks never see this structure; they see only port
 * names (small integers) that index into their ipc_space.  The kernel maps
 * those names to ipc_port pointers internally.
 *
 * Reference: CMU Mach 3.0 paper (Accetta et al., 1986) §3 — IPC Overview;
 *            OSF MK ipc/ipc_port.h for field naming conventions.
 */

#ifndef IPC_PORT_H
#define IPC_PORT_H

#include <stdint.h>
#include <stdatomic.h>
#include "mach/mach_types.h"

/* Forward declarations */
struct task;
struct ipc_mqueue;

/* -------------------------------------------------------------------------
 * Port type flags
 *
 * A port may be in one of three states:
 *   IPC_PORT_TYPE_ACTIVE  — normal operating port; messages may be sent.
 *   IPC_PORT_TYPE_DEAD    — the receive right has been destroyed; all send
 *                           rights refer to a dead-name entry.  mach_msg()
 *                           to a dead port returns MACH_SEND_INVALID_DEST.
 *   IPC_PORT_TYPE_KERNEL  — a port whose receive right is held by the kernel
 *                           itself (e.g. the task port, thread port).
 *
 * CMU Mach 3.0 paper §3.1: "When the receive right for a port is destroyed,
 * all outstanding send rights become dead names."
 * ------------------------------------------------------------------------- */

typedef enum {
    IPC_PORT_TYPE_ACTIVE = 0,
    IPC_PORT_TYPE_DEAD   = 1,
    IPC_PORT_TYPE_KERNEL = 2,
} ipc_port_type_t;

/* -------------------------------------------------------------------------
 * struct ipc_port — the kernel-internal port object
 *
 * Lifetime: allocated when a RECEIVE right is created; freed when the receive
 * right is destroyed (which also transitions all send rights to dead names).
 *
 * Locking: ip_lock must be held to read or write any field other than ip_type
 * (which is written atomically during port death).
 * ------------------------------------------------------------------------- */

struct ipc_port {
    /*
     * ip_type — the current state of this port.
     * Written atomically so that ip_type can be read lock-free as a fast-path
     * dead-port check before acquiring ip_lock.
     */
    atomic_int          ip_type;        /* ipc_port_type_t */

    /*
     * ip_lock — spinlock protecting all mutable fields below.
     *
     * TODO: Replace with a proper sleep lock once the thread scheduler is
     *       implemented (Phase 2).  A spinlock is acceptable here during
     *       Phase 1 because we have a single CPU and no preemption.
     */
    atomic_flag         ip_lock;        /* spinlock: test-and-set */
    uint64_t            ip_saved_flags; /* IRQ flags saved by ipc_port_lock */

    /*
     * ip_send_rights — reference count for outstanding send rights.
     *
     * CMU Mach 3.0 paper §3.3: "The kernel tracks send-right reference
     * counts so it can notify the receiver when the last send right is
     * destroyed (no-senders notification)."
     *
     * When ip_send_rights drops to zero the kernel may (optionally) deliver
     * a no-senders notification to the port's receiver.
     */
    uint32_t            ip_send_rights;

    /*
     * ip_receiver — the task that holds the receive right for this port.
     *
     * NULL if ip_type == IPC_PORT_TYPE_KERNEL (kernel is the receiver) or
     * IPC_PORT_TYPE_DEAD.
     */
    struct task        *ip_receiver;

    /*
     * ip_messages — the message queue for this port.
     *
     * Incoming messages are appended here by the sender; the receiver
     * dequeues them via mach_msg(MACH_RCV_MSG).
     *
     * The queue is embedded by value to avoid a separate allocation per port.
     * See kernel/ipc/ipc_mqueue.h for the queue structure.
     *
     * TODO: In real Mach, the message queue also carries a set of threads
     *       blocked waiting to receive.  Add that in Phase 2 when we have a
     *       thread scheduler (see ipc_mqueue.h for the TODO marker).
     */
    struct ipc_mqueue  *ip_messages;    /* points into embedded storage below */

    /*
     * ip_seqno — monotonically increasing sequence number.
     * Used to order received messages and detect stale receives.
     * OSF MK ipc_port.h documents this field.
     */
    uint32_t            ip_seqno;
};

/* -------------------------------------------------------------------------
 * ipc_port operations (implemented in kernel/ipc/ipc_port.c)
 * ------------------------------------------------------------------------- */

/*
 * ipc_port_alloc — allocate and initialise a new active port.
 * Returns NULL if the system is out of memory.
 * The calling task becomes the initial receiver.
 */
struct ipc_port *ipc_port_alloc(struct task *receiver);

/*
 * ipc_port_destroy — destroy a port; transitions all send rights to dead names.
 * Caller must NOT hold ip_lock.
 */
void ipc_port_destroy(struct ipc_port *port);

/* Interrupt-safe spinlock helpers */
static inline void ipc_port_lock(struct ipc_port *port)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    while (atomic_flag_test_and_set_explicit(&port->ip_lock,
                                             memory_order_acquire))
        ; /* spin */
    port->ip_saved_flags = flags;
}

static inline void ipc_port_unlock(struct ipc_port *port)
{
    uint64_t flags = port->ip_saved_flags;
    atomic_flag_clear_explicit(&port->ip_lock, memory_order_release);
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
}

#endif /* IPC_PORT_H */
