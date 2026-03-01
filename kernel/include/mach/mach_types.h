/*
 * kernel/include/mach/mach_types.h — Core Mach 3.0 type definitions for UNHOX
 *
 * Reference: Accetta et al., "Mach: A New Kernel Foundation for UNIX
 *            Development", USENIX Summer Conference, 1986.
 *            (Hereafter cited as "CMU Mach 3.0 paper".)
 *
 * These types mirror the CMU Mach 3.0 interface as closely as practical on a
 * modern LP64 target.  Where we deviate from the historical definitions an
 * explicit comment explains the reason.
 */

#ifndef MACH_MACH_TYPES_H
#define MACH_MACH_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Fundamental integer sizes
 * ------------------------------------------------------------------------- */

typedef uint32_t mach_port_name_t;   /* user-visible port name (table index) */
typedef uint32_t mach_port_t;        /* synonym used in most interfaces       */

/*
 * mach_port_right_t — the type of right a task holds for a given port name.
 *
 * CMU Mach 3.0 paper §3.1: "Port rights are the capabilities by which tasks
 * refer to ports.  A task may hold a receive right, send rights, or a
 * send-once right for a given port."
 */
typedef uint32_t mach_port_right_t;

#define MACH_PORT_RIGHT_SEND        ((mach_port_right_t) 0)
#define MACH_PORT_RIGHT_RECEIVE     ((mach_port_right_t) 1)
#define MACH_PORT_RIGHT_SEND_ONCE   ((mach_port_right_t) 2)
#define MACH_PORT_RIGHT_PORT_SET    ((mach_port_right_t) 3)
#define MACH_PORT_RIGHT_DEAD_NAME   ((mach_port_right_t) 4)

/* The null port name — analogous to a null pointer */
#define MACH_PORT_NULL              ((mach_port_t) 0)
/* A port name that is dead (the port has been destroyed) */
#define MACH_PORT_DEAD              ((mach_port_t) ~0U)

/* -------------------------------------------------------------------------
 * Message types
 * ------------------------------------------------------------------------- */

typedef uint32_t mach_msg_size_t;    /* byte count of a message              */
typedef uint32_t mach_msg_id_t;      /* caller-defined message identifier    */
typedef uint32_t mach_msg_bits_t;    /* packed flags in mach_msg_header_t    */
typedef uint32_t mach_msg_type_name_t;

/*
 * mach_msg_header_t — the fixed header present at the start of every Mach
 * message.
 *
 * CMU Mach 3.0 paper §3.2: "Every message begins with a fixed-size header
 * that contains the message type, size, destination port, reply port, and a
 * message identifier."
 *
 * msgh_bits encodes the descriptor types for remote and local ports.
 * msgh_remote_port is the destination (send right required).
 * msgh_local_port  is the reply port (often a send-once right).
 * msgh_voucher_port is reserved; set to MACH_PORT_NULL.
 * msgh_id is an opaque integer the sender chooses; used to demultiplex RPCs.
 */
typedef struct {
    mach_msg_bits_t  msgh_bits;
    mach_msg_size_t  msgh_size;
    mach_port_t      msgh_remote_port;
    mach_port_t      msgh_local_port;
    mach_port_t      msgh_voucher_port;
    mach_msg_id_t    msgh_id;
} mach_msg_header_t;

/*
 * mach_msg_body_t — descriptor for complex (out-of-line or port-carrying)
 * messages.  Immediately follows mach_msg_header_t when
 * MACH_MSGH_BITS_COMPLEX is set in msgh_bits.
 *
 * msgh_descriptor_count gives the number of typed descriptors that follow
 * this structure before the inline data payload.
 */
typedef struct {
    uint32_t msgh_descriptor_count;
} mach_msg_body_t;

/* Bit flag indicating the message carries typed descriptors */
#define MACH_MSGH_BITS_COMPLEX      ((mach_msg_bits_t) 0x80000000U)

/* Accessors for the remote/local port type fields packed into msgh_bits */
#define MACH_MSGH_BITS_REMOTE(bits) ((bits) & 0x1f)
#define MACH_MSGH_BITS_LOCAL(bits)  (((bits) >> 8) & 0x1f)
#define MACH_MSGH_BITS(remote, local) \
    (((local) << 8) | (remote))

/* -------------------------------------------------------------------------
 * Message return codes
 *
 * Returned by mach_msg() to indicate success or the specific failure mode.
 * CMU Mach 3.0 paper §3.4 enumerates these.
 * ------------------------------------------------------------------------- */

typedef uint32_t mach_msg_return_t;

#define MACH_MSG_SUCCESS            ((mach_msg_return_t) 0x00000000)

/* Send-side errors */
#define MACH_SEND_IN_PROGRESS       ((mach_msg_return_t) 0x10000001)
#define MACH_SEND_INVALID_DATA      ((mach_msg_return_t) 0x10000002)
#define MACH_SEND_INVALID_DEST      ((mach_msg_return_t) 0x10000003)
#define MACH_SEND_TIMED_OUT         ((mach_msg_return_t) 0x10000004)
#define MACH_SEND_INVALID_VOUCHER   ((mach_msg_return_t) 0x10000005)
#define MACH_SEND_INTERRUPTED       ((mach_msg_return_t) 0x10000007)
#define MACH_SEND_MSG_TOO_SMALL     ((mach_msg_return_t) 0x10000008)
#define MACH_SEND_INVALID_REPLY     ((mach_msg_return_t) 0x10000009)
#define MACH_SEND_INVALID_RIGHT     ((mach_msg_return_t) 0x1000000a)
#define MACH_SEND_INVALID_NOTIFY    ((mach_msg_return_t) 0x1000000b)
#define MACH_SEND_INVALID_MEMORY    ((mach_msg_return_t) 0x1000000c)
#define MACH_SEND_NO_BUFFER         ((mach_msg_return_t) 0x1000000d)
#define MACH_SEND_TOO_LARGE         ((mach_msg_return_t) 0x1000000e)
#define MACH_SEND_INVALID_TYPE      ((mach_msg_return_t) 0x1000000f)
#define MACH_SEND_INVALID_HEADER    ((mach_msg_return_t) 0x10000010)

/* Receive-side errors */
#define MACH_RCV_IN_PROGRESS        ((mach_msg_return_t) 0x10004001)
#define MACH_RCV_INVALID_NAME       ((mach_msg_return_t) 0x10004002)
#define MACH_RCV_TIMED_OUT          ((mach_msg_return_t) 0x10004003)
#define MACH_RCV_TOO_LARGE          ((mach_msg_return_t) 0x10004004)
#define MACH_RCV_INTERRUPTED        ((mach_msg_return_t) 0x10004005)
#define MACH_RCV_PORT_CHANGED       ((mach_msg_return_t) 0x10004006)
#define MACH_RCV_INVALID_NOTIFY     ((mach_msg_return_t) 0x10004007)
#define MACH_RCV_INVALID_DATA       ((mach_msg_return_t) 0x10004008)
#define MACH_RCV_PORT_DIED          ((mach_msg_return_t) 0x10004009)
#define MACH_RCV_IN_SET             ((mach_msg_return_t) 0x1000400a)
#define MACH_RCV_HEADER_ERROR       ((mach_msg_return_t) 0x1000400b)
#define MACH_RCV_BODY_ERROR         ((mach_msg_return_t) 0x1000400c)
#define MACH_RCV_INVALID_TYPE       ((mach_msg_return_t) 0x1000400d)
#define MACH_RCV_SCATTER_SMALL      ((mach_msg_return_t) 0x1000400e)
#define MACH_RCV_INVALID_TRAILER    ((mach_msg_return_t) 0x1000400f)
#define MACH_RCV_IN_KERNEL          ((mach_msg_return_t) 0x10004010)

/* -------------------------------------------------------------------------
 * Kernel return type
 *
 * kern_return_t is used by all kernel operations other than mach_msg().
 * CMU Mach 3.0 paper §2: returned by task_create, vm_allocate, etc.
 * ------------------------------------------------------------------------- */

typedef int32_t kern_return_t;

#define KERN_SUCCESS                ((kern_return_t)  0)
#define KERN_INVALID_ADDRESS        ((kern_return_t)  1)
#define KERN_PROTECTION_FAILURE     ((kern_return_t)  2)
#define KERN_NO_SPACE               ((kern_return_t)  3)
#define KERN_INVALID_ARGUMENT       ((kern_return_t)  4)
#define KERN_FAILURE                ((kern_return_t)  5)
#define KERN_RESOURCE_SHORTAGE      ((kern_return_t)  6)
#define KERN_NOT_RECEIVER           ((kern_return_t)  7)
#define KERN_NO_ACCESS              ((kern_return_t)  8)
#define KERN_MEMORY_FAILURE         ((kern_return_t)  9)
#define KERN_MEMORY_ERROR           ((kern_return_t) 10)
#define KERN_ALREADY_IN_SET         ((kern_return_t) 11)
#define KERN_NOT_IN_SET             ((kern_return_t) 12)
#define KERN_NAME_EXISTS            ((kern_return_t) 13)
#define KERN_ABORTED                ((kern_return_t) 14)
#define KERN_INVALID_NAME           ((kern_return_t) 15)
#define KERN_INVALID_TASK           ((kern_return_t) 16)
#define KERN_INVALID_RIGHT          ((kern_return_t) 17)
#define KERN_INVALID_VALUE          ((kern_return_t) 18)
#define KERN_UREFS_OVERFLOW         ((kern_return_t) 19)
#define KERN_INVALID_CAPABILITY     ((kern_return_t) 20)
#define KERN_RIGHT_EXISTS           ((kern_return_t) 21)
#define KERN_INVALID_HOST           ((kern_return_t) 22)
#define KERN_MEMORY_PRESENT         ((kern_return_t) 23)
#define KERN_MEMORY_DATA_MOVED      ((kern_return_t) 24)
#define KERN_MEMORY_RESTART_COPY    ((kern_return_t) 25)
#define KERN_INVALID_PROCESSOR_SET  ((kern_return_t) 26)
#define KERN_POLICY_LIMIT           ((kern_return_t) 27)
#define KERN_INVALID_POLICY         ((kern_return_t) 28)
#define KERN_INVALID_OBJECT         ((kern_return_t) 29)
#define KERN_ALREADY_WAITING        ((kern_return_t) 30)
#define KERN_DEFAULT_SET            ((kern_return_t) 31)
#define KERN_EXCEPTION_PROTECTED    ((kern_return_t) 32)
#define KERN_INVALID_LEDGER         ((kern_return_t) 33)
#define KERN_INVALID_MEMORY_CONTROL ((kern_return_t) 34)
#define KERN_INVALID_SECURITY       ((kern_return_t) 35)
#define KERN_NOT_DEPRESSED          ((kern_return_t) 36)
#define KERN_TERMINATED             ((kern_return_t) 37)
#define KERN_LOCK_SET_DESTROYED     ((kern_return_t) 38)
#define KERN_LOCK_UNSTABLE          ((kern_return_t) 39)
#define KERN_LOCK_OWNED             ((kern_return_t) 40)
#define KERN_LOCK_OWNED_SELF        ((kern_return_t) 41)
#define KERN_SEMAPHORE_DESTROYED    ((kern_return_t) 42)
#define KERN_RPC_SERVER_TERMINATED  ((kern_return_t) 43)
#define KERN_RPC_TERMINATE_ORPHAN   ((kern_return_t) 44)
#define KERN_RPC_CONTINUE_ORPHAN    ((kern_return_t) 45)
#define KERN_NOT_SUPPORTED          ((kern_return_t) 46)
#define KERN_NODE_DOWN              ((kern_return_t) 47)
#define KERN_NOT_WAITING            ((kern_return_t) 48)
#define KERN_OPERATION_TIMED_OUT    ((kern_return_t) 49)

/* -------------------------------------------------------------------------
 * Task and thread handles
 *
 * In Mach, tasks and threads are accessed through port send rights.
 * task_t and thread_t are port names in the current task's port space.
 *
 * CMU Mach 3.0 paper §4: "A task is manipulated by sending messages to
 * its task port.  The kernel creates a task port when a task is created."
 * ------------------------------------------------------------------------- */

/*
 * task_t — opaque handle to a Mach task.
 *
 * From userspace this is a send right to the task's kernel-managed task port.
 * Inside the kernel we use struct task * directly; task_t in kernel context
 * is a typedef alias kept for API symmetry.
 */
typedef mach_port_t task_t;

/*
 * thread_t — opaque handle to a Mach thread.
 *
 * Analogous to task_t: a send right to the thread's kernel port.
 */
typedef mach_port_t thread_t;

/* Special task/thread values */
#define TASK_NULL               ((task_t)   MACH_PORT_NULL)
#define THREAD_NULL             ((thread_t) MACH_PORT_NULL)

/* -------------------------------------------------------------------------
 * Virtual memory types
 *
 * CMU Mach 3.0 paper §5: "The virtual memory system provides each task with
 * a large, sparse address space."
 *
 * On x86-64 we use 64-bit quantities throughout.  Historical Mach used
 * natural-word-size types (vm_offset_t was 32 bits on 32-bit machines).
 * ------------------------------------------------------------------------- */

/*
 * vm_address_t — a virtual address in a task's address space.
 * Must be wide enough to hold any user or kernel virtual address.
 */
typedef uintptr_t vm_address_t;

/*
 * vm_offset_t — a byte offset within an address space or memory object.
 * Signed to allow expressing negative offsets.
 */
typedef intptr_t  vm_offset_t;

/*
 * vm_size_t — a byte count for VM operations (allocations, mappings, etc.).
 */
typedef size_t    vm_size_t;

/*
 * vm_prot_t — protection bits for a VM region.
 * CMU Mach 3.0 paper §5.2: read, write, execute are independent bits.
 */
typedef int vm_prot_t;

#define VM_PROT_NONE        ((vm_prot_t) 0x00)
#define VM_PROT_READ        ((vm_prot_t) 0x01)
#define VM_PROT_WRITE       ((vm_prot_t) 0x02)
#define VM_PROT_EXECUTE     ((vm_prot_t) 0x04)
#define VM_PROT_DEFAULT     (VM_PROT_READ | VM_PROT_WRITE)
#define VM_PROT_ALL         (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE)

/* Page size — architecture-specific; x86-64 base page is 4 KiB */
#define VM_PAGE_SIZE        ((vm_size_t) 4096)
#define VM_PAGE_SHIFT       12

/* Align an address down/up to a page boundary */
#define VM_PAGE_TRUNC(addr) ((vm_address_t)(addr) & ~(VM_PAGE_SIZE - 1))
#define VM_PAGE_ROUND(addr) VM_PAGE_TRUNC((vm_address_t)(addr) + VM_PAGE_SIZE - 1)

#endif /* MACH_MACH_TYPES_H */
