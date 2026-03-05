/*
 * user/libc/mach_msg.c — Userspace Mach IPC wrapper for UNHOX
 */

#include "mach_msg.h"
#include "syscall.h"

kern_return_t mach_msg_send_user(mach_msg_header_t *msg,
                                  mach_msg_size_t size,
                                  mach_port_name_t dest)
{
    long ret = mach_msg_send_syscall(msg, (long)size, (long)dest);
    return (ret == 0) ? 0 : -1;
}

kern_return_t mach_msg_recv_user(mach_port_name_t port,
                                  void *buf,
                                  mach_msg_size_t buf_size,
                                  mach_msg_size_t *out_size)
{
    long ret = mach_msg_recv_syscall((long)port, buf, (long)buf_size);
    if (ret < 0)
        return -1;
    if (out_size)
        *out_size = (mach_msg_size_t)ret;
    return 0;
}

mach_port_name_t mach_port_allocate_user(void)
{
    long ret = port_alloc_syscall();
    if (ret < 0)
        return MACH_PORT_NULL;
    return (mach_port_name_t)ret;
}
