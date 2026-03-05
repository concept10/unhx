/*
 * user/libc/mach_msg.h — Userspace Mach IPC wrapper for UNHOX
 *
 * Provides the message send/receive API on top of SYS_MACH_MSG_SEND/RECV.
 * The mach_msg_header_t layout must match the kernel's exactly.
 */

#ifndef USER_MACH_MSG_H
#define USER_MACH_MSG_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t mach_port_name_t;
typedef uint32_t mach_port_t;
typedef uint32_t mach_msg_size_t;
typedef uint32_t mach_msg_id_t;
typedef uint32_t mach_msg_bits_t;
typedef uint32_t mach_msg_return_t;
typedef int32_t  kern_return_t;

typedef struct {
    mach_msg_bits_t  msgh_bits;
    mach_msg_size_t  msgh_size;
    mach_port_t      msgh_remote_port;
    mach_port_t      msgh_local_port;
    mach_port_t      msgh_voucher_port;
    mach_msg_id_t    msgh_id;
} mach_msg_header_t;

#define MACH_PORT_NULL  ((mach_port_t)0)
#define MACH_MSG_SUCCESS 0

/* Send a message to a destination port */
kern_return_t mach_msg_send_user(mach_msg_header_t *msg,
                                  mach_msg_size_t size,
                                  mach_port_name_t dest);

/* Receive a message (blocking) on a port the caller owns */
kern_return_t mach_msg_recv_user(mach_port_name_t port,
                                  void *buf,
                                  mach_msg_size_t buf_size,
                                  mach_msg_size_t *out_size);

/* Allocate a new Mach port (send+receive rights) */
mach_port_name_t mach_port_allocate_user(void);

#endif /* USER_MACH_MSG_H */
