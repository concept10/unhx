/*
 * servers/vfs/vfs_server.c — VFS server kernel thread for UNHOX
 *
 * Runs as a kernel thread (ring 0). Initialises the ramfs, allocates a
 * receive port, registers as "com.unhox.vfs" with the bootstrap server, then
 * blocks in a Mach message receive loop serving VFS_MSG_OPEN / VFS_MSG_READ /
 * VFS_MSG_CLOSE requests.
 *
 * Reference: OSF MK servers/vfs/vfs_server.c for the original server loop.
 */

#include "vfs/vfs_msg.h"
#include "vfs/ramfs.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/klib.h"
#include "kern/task.h"
#include "bootstrap/bootstrap.h"

extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern struct thread *sched_current(void);

/* -------------------------------------------------------------------------
 * Global VFS port — set before entering the message loop.
 * ------------------------------------------------------------------------- */

struct ipc_port *vfs_port = (void *)0;

/* -------------------------------------------------------------------------
 * vfs_server_main — VFS server thread entry point
 * ------------------------------------------------------------------------- */

void vfs_server_main(void)
{
    extern void serial_puthex(uint64_t val);
    serial_putstr("[vfs-server] thread ID=");
    serial_puthex((uint64_t)sched_current());
    serial_putstr("\r\n");
    
    serial_putstr("[vfs] initialising VFS/ramfs server\r\n");

    ramfs_init();

    /* Allocate the VFS receive port */
    vfs_port = ipc_port_alloc(kernel_task_ptr());
    if (!vfs_port) {
        serial_putstr("[vfs] FATAL: could not allocate VFS port\r\n");
        for (;;)
            __asm__ volatile ("hlt");
    }

    /* Wait for the bootstrap server to be ready */
    while (!bootstrap_port)
        for (volatile int i = 0; i < 10000; i++)
            ;

    /* Register as "com.unhox.vfs" */
    bootstrap_register_msg_t reg;
    kmemset(&reg, 0, sizeof(reg));
    reg.hdr.msgh_size = sizeof(reg);
    reg.hdr.msgh_id   = BOOTSTRAP_MSG_REGISTER;
    kstrncpy(reg.name, "com.unhox.vfs", BOOTSTRAP_NAME_MAX);
    reg.service_port  = (uint64_t)vfs_port;
    ipc_mqueue_send(bootstrap_port->ip_messages, &reg, sizeof(reg));

    serial_putstr("[vfs] VFS server ready\r\n");

    /*
     * Message receive loop.
     * Buffer sized for the largest request (vfs_write_msg_t).
     */
    uint8_t buf[sizeof(vfs_write_msg_t) + 16];

    for (;;) {
        serial_putstr("[vfs] Calling receive...\r\n");
        mach_msg_size_t msg_size = 0;
        mach_msg_return_t mr = ipc_mqueue_receive(
            vfs_port->ip_messages,
            buf, sizeof(buf),
            &msg_size,
            1 /* blocking */);

        serial_putstr("[vfs] Receive returned:");
        serial_puthex(mr);
        serial_putstr(" size=");
        serial_puthex(msg_size);
        serial_putstr("\r\n");

        if (mr != MACH_MSG_SUCCESS)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

        serial_putstr("[vfs] Received message id=");
        serial_puthex(hdr->msgh_id);
        serial_putstr(" size=");
        serial_puthex(msg_size);
        serial_putstr("\r\n");

        switch (hdr->msgh_id) {

        case VFS_MSG_OPEN: {
            if (msg_size < sizeof(vfs_open_msg_t))
                break;
            vfs_open_msg_t *req = (vfs_open_msg_t *)buf;
            req->path[VFS_PATH_MAX - 1] = '\0';

            int fd = ramfs_open(req->path);

            serial_putstr("[vfs] OPEN: ");
            serial_putstr(req->path);
            serial_putstr(fd >= 0 ? " -> ok\r\n" : " -> not found\r\n");

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (fd >= 0) ? VFS_SUCCESS : VFS_NOT_FOUND;
                reply.result        = fd;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        case VFS_MSG_READ: {
            if (msg_size < sizeof(vfs_read_msg_t))
                break;
            vfs_read_msg_t *req = (vfs_read_msg_t *)buf;

            serial_putstr("[vfs] READ: fd=");
            serial_puthex(req->fd);
            serial_putstr(" count=");
            serial_puthex(req->count);
            serial_putstr(" offset=");
            serial_puthex(req->offset);
            serial_putstr("\r\n");

            uint8_t  data_buf[VFS_DATA_MAX];
            uint32_t want = req->count < VFS_DATA_MAX ? req->count : VFS_DATA_MAX;
            int      n    = ramfs_read(req->fd, data_buf, want, req->offset);

            serial_putstr("[vfs] ramfs_read returned n=");
            serial_puthex(n);
            serial_putstr(" reply_port=");
            serial_puthex((uint64_t)req->reply_port);
            serial_putstr("\r\n");

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (n >= 0) ? VFS_SUCCESS : VFS_BAD_FD;
                reply.result        = (n >= 0) ? n : 0;
                if (n > 0)
                    kmemcpy(reply.data, data_buf, (uint32_t)n);

                serial_putstr("[vfs] Sending READ reply to port ");
                serial_puthex((uint64_t)req->reply_port);
                serial_putstr("\r\n");

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
                
                serial_putstr("[vfs] READ reply sent\r\n");
            } else {
                serial_putstr("[vfs] READ reply_port is NULL, not sending\r\n");
            }
            break;
        }

        case VFS_MSG_CLOSE: {
            if (msg_size < sizeof(vfs_close_msg_t))
                break;
            vfs_close_msg_t *req = (vfs_close_msg_t *)buf;
            ramfs_close(req->fd);
            break;
        }

        case VFS_MSG_WRITE: {
            if (msg_size < sizeof(vfs_write_msg_t))
                break;
            vfs_write_msg_t *req = (vfs_write_msg_t *)buf;

            int n = ramfs_write(req->fd, req->data, req->count);

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (n >= 0) ? VFS_SUCCESS : VFS_BAD_FD;
                reply.result        = (n >= 0) ? n : 0;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        case VFS_MSG_STAT: {
            if (msg_size < sizeof(vfs_stat_msg_t))
                break;
            vfs_stat_msg_t *req = (vfs_stat_msg_t *)buf;

            uint32_t size = 0;
            int ret = ramfs_stat(req->fd, &size);

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (ret >= 0) ? VFS_SUCCESS : VFS_BAD_FD;
                reply.result        = size;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        case VFS_MSG_READDIR: {
            if (msg_size < sizeof(vfs_readdir_msg_t))
                break;
            vfs_readdir_msg_t *req = (vfs_readdir_msg_t *)buf;

            uint32_t count = 0;
            int ret = ramfs_readdir(req->fd, NULL, 0, &count);

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (ret >= 0) ? VFS_SUCCESS : VFS_BAD_FD;
                reply.result        = count;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        case VFS_MSG_MKDIR: {
            if (msg_size < sizeof(vfs_mkdir_msg_t))
                break;
            vfs_mkdir_msg_t *req = (vfs_mkdir_msg_t *)buf;
            req->path[VFS_PATH_MAX - 1] = '\0';

            int fd = ramfs_mkdir((const char *)req->path, req->mode);

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (fd >= 0) ? VFS_SUCCESS : VFS_NOT_FOUND;
                reply.result        = fd;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        case VFS_MSG_UNLINK: {
            if (msg_size < sizeof(vfs_unlink_msg_t))
                break;
            vfs_unlink_msg_t *req = (vfs_unlink_msg_t *)buf;
            req->path[VFS_PATH_MAX - 1] = '\0';

            int ret = ramfs_unlink((const char *)req->path);

            if (req->reply_port) {
                vfs_reply_msg_t reply;
                kmemset(&reply, 0, sizeof(reply));
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_id   = VFS_MSG_REPLY;
                reply.retcode       = (ret >= 0) ? VFS_SUCCESS : VFS_NOT_FOUND;
                reply.result        = 0;

                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        default:
            serial_putstr("[vfs] unknown message\r\n");
            break;
        }
    }
}
