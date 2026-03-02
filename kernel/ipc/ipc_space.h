/*
 * kernel/ipc/ipc_space.h — A task's port name space for UNHOX
 *
 * ipc_space is the per-task table that maps port names (small integers that
 * userspace programs use) to kernel-internal port objects (ipc_port pointers).
 *
 * =========================================================================
 * SECURITY MODEL — WHY THIS INDIRECTION EXISTS
 * =========================================================================
 *
 * CMU Mach 3.0 paper (Accetta et al., 1986) §3.2:
 *
 *   "Port names are local to a task's name space.  The same port may have
 *    different names in different tasks, and may be unnamed (and therefore
 *    inaccessible) in yet others.  The kernel translates port names to port
 *    identifiers internally."
 *
 * The fundamental security invariant is:
 *
 *   Userspace tasks NEVER see kernel pointers.
 *
 * A task refers to a port by a small integer (the port name, stored as
 * mach_port_name_t).  The kernel looks up that integer in the task's ipc_space
 * to obtain the struct ipc_port *.  This means:
 *
 *  1. A task cannot forge a port capability by guessing a kernel address.
 *     All it can do is present a name; if the name is not in its space, the
 *     operation fails with KERN_INVALID_NAME.
 *
 *  2. Revoking access is O(1): the kernel removes the ipc_entry from the
 *     space; subsequent uses of the old name fail immediately.
 *
 *  3. Port rights can be transferred between tasks by the kernel (via
 *     out-of-line port descriptors in messages) without the sender knowing
 *     the receiver's address space layout.
 *
 * This is the capability model that makes Mach IPC a secure building block.
 *
 * Reference: OSF MK ipc/ipc_space.h for field names and locking discipline.
 */

#ifndef IPC_SPACE_H
#define IPC_SPACE_H

#include <stdint.h>
#include <stdatomic.h>
#include "mach/mach_types.h"
#include "ipc_entry.h"

/* Forward declaration */
struct task;

/* -------------------------------------------------------------------------
 * Limits
 * ------------------------------------------------------------------------- */

/*
 * IPC_SPACE_MAX_ENTRIES — maximum number of port names per task.
 *
 * Real Mach uses a dynamically-sized hash table (ipc_table).  For Phase 1
 * we use a fixed-size flat array to keep the implementation simple.  The
 * array size is tunable at compile time.
 *
 * TODO (Phase 2): Replace with the CMU/OSF dynamic table scheme described in
 *                 OSF MK ipc/ipc_table.h so that port spaces can grow on
 *                 demand without wasting memory in tasks with few ports.
 */
#ifndef IPC_SPACE_MAX_ENTRIES
#define IPC_SPACE_MAX_ENTRIES   256
#endif

/* Index 0 is permanently reserved for MACH_PORT_NULL */
#define IPC_SPACE_FIRST_VALID   1

/* -------------------------------------------------------------------------
 * struct ipc_space — a task's port name space
 * ------------------------------------------------------------------------- */

struct ipc_space {
    /*
     * is_lock — spinlock protecting all mutable fields.
     *
     * Must be held when:
     *   - reading or writing any is_table entry
     *   - modifying is_free_count or is_next_free
     *   - transitioning is_active
     *
     * TODO (Phase 2): Replace with a read/write lock once we have a scheduler
     *                 that can put threads to sleep.  Most operations are
     *                 lookups (reads); a rwlock would reduce contention.
     */
    atomic_flag         is_lock;
    uint64_t            is_saved_flags; /* IRQ flags saved by ipc_space_lock */

    /*
     * is_active — false once the space has been torn down (task_destroy).
     * After teardown, ipc_space_lookup() always returns NULL.
     */
    int                 is_active;

    /*
     * is_task — back-pointer to the task that owns this space.
     * Used to deliver no-senders notifications and for debugging.
     */
    struct task        *is_task;

    /*
     * is_table — the flat array of port name entries.
     *
     * Index == port name.  Index 0 is always IPC_PORT_NULL (never allocated).
     * A free slot has ie_bits == IE_BITS_NONE and ie_object == NULL.
     */
    struct ipc_entry    is_table[IPC_SPACE_MAX_ENTRIES];

    /*
     * is_table_size — number of slots in is_table (== IPC_SPACE_MAX_ENTRIES
     * for Phase 1's static allocation).
     */
    uint32_t            is_table_size;

    /*
     * is_free_count — number of free slots currently available.
     */
    uint32_t            is_free_count;

    /*
     * is_next_free — hint: start the free-slot search here.
     * Not guaranteed to be free; always verify ie_bits before using.
     */
    mach_port_name_t    is_next_free;
};

/* -------------------------------------------------------------------------
 * ipc_space operations (implemented in kernel/ipc/ipc_space.c)
 * ------------------------------------------------------------------------- */

/*
 * ipc_space_create — allocate and initialise a new, empty port space for task.
 * Returns NULL on allocation failure.
 */
struct ipc_space *ipc_space_create(struct task *task);

/*
 * ipc_space_destroy — deallocate all entries and free the space.
 * Called by task_destroy().  Caller must not hold is_lock.
 */
void ipc_space_destroy(struct ipc_space *space);

/*
 * ipc_space_alloc_name — allocate a free port name in the space.
 * On success stores the name in *namep and returns KERN_SUCCESS.
 * Returns KERN_NO_SPACE if the table is full.
 * Caller must hold is_lock.
 */
kern_return_t ipc_space_alloc_name(struct ipc_space *space,
                                   mach_port_name_t *namep);

/*
 * ipc_space_lookup — look up a port name in the space.
 * Returns a pointer to the ipc_entry on success, NULL if not found.
 * Caller must hold is_lock.
 */
struct ipc_entry *ipc_space_lookup(struct ipc_space *space,
                                   mach_port_name_t  name);

/* Interrupt-safe spinlock helpers */
static inline void ipc_space_lock(struct ipc_space *space)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    while (atomic_flag_test_and_set_explicit(&space->is_lock,
                                             memory_order_acquire))
        ; /* spin */
    space->is_saved_flags = flags;
}

static inline void ipc_space_unlock(struct ipc_space *space)
{
    uint64_t flags = space->is_saved_flags;
    atomic_flag_clear_explicit(&space->is_lock, memory_order_release);
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory");
}

#endif /* IPC_SPACE_H */
