/*
 * servers/bsd/bsd_server.c — Minimal BSD server for UNHOX
 *
 * Runs as a kernel thread. Maintains a simple file descriptor table where
 * fd 0/1/2 map to the serial console. Registers as "com.unhox.bsd" with
 * the bootstrap server and serves BSD_MSG_WRITE / BSD_MSG_READ requests.
 *
 * Reference: OSF MK servers/bsd/bsd_server.c
 */

#include "bsd/bsd_msg.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/klib.h"
#include "kern/kalloc.h"
#include "kern/task.h"
#include "kern/sched.h"
#include "kern/thread.h"
#include "kern/elf.h"
#include "kern/elf_load.h"
#include "kern/kern.h"
#include "vfs/vfs_msg.h"
#include "vm/vm_map.h"
#include "platform/paging.h"
#include "platform/idt.h"
#include "bootstrap/bootstrap.h"

#define BSD_EXEC_MAX_IMAGE   (512 * 1024)
#define BSD_USER_STACK_BASE  0x7FFF0000ULL
#define BSD_USER_STACK_SIZE  0x10000ULL

extern void serial_putstr(const char *s);
extern void serial_putchar(char c);
extern void serial_puthex(uint64_t val);
extern char serial_getchar(void);

/* -------------------------------------------------------------------------
 * Global BSD port
 * ------------------------------------------------------------------------- */

struct ipc_port *bsd_port = (void *)0;

static int bsd_vfs_open(const char *path, struct ipc_port *reply)
{
    int ret = -1;

    if (!path || !vfs_port || !reply) {
        serial_putstr("[bsd-vfs-open] ERROR: path or vfs_port is NULL\r\n");
        return -1;
    }

    vfs_open_msg_t req;
    kmemset(&req, 0, sizeof(req));
    req.hdr.msgh_size = sizeof(req);
    req.hdr.msgh_id   = VFS_MSG_OPEN;
    kstrncpy(req.path, path, VFS_PATH_MAX);
    req.reply_port    = (uint64_t)reply;

    serial_putstr("[bsd-vfs-open] sending OPEN message...\r\n");
    if (ipc_mqueue_send(vfs_port->ip_messages, &req, sizeof(req)) != MACH_MSG_SUCCESS) {
        serial_putstr("[bsd-vfs-open] ERROR: send failed\r\n");
        return -1;
    }

    serial_putstr("[bsd-vfs-open] waiting for reply...\r\n");
    vfs_reply_msg_t rep;
    mach_msg_size_t out_size = 0;
    int got_reply = 0;
    for (int spins = 0; spins < 200000; spins++) {
        mach_msg_return_t mr = ipc_mqueue_receive(
            reply->ip_messages, &rep, sizeof(rep), &out_size, 0 /* non-blocking */);
        if (mr == MACH_MSG_SUCCESS) {
            got_reply = 1;
            break;
        }
        if ((spins % 256) == 0)
            sched_yield();
    }
    if (!got_reply) {
        serial_putstr("[bsd-vfs-open] ERROR: timed out waiting for OPEN reply\r\n");
        goto out;
    }

    serial_putstr("[bsd-vfs-open] got reply, retcode=");
    serial_puthex((uint64_t)rep.retcode);
    serial_putstr(" result=");
    serial_puthex((uint64_t)rep.result);
    serial_putstr("\r\n");

    if (rep.retcode != VFS_SUCCESS)
        goto out;

    ret = rep.result;

out:
    return ret;
}

static int bsd_vfs_read_chunk(int fd, uint32_t offset, uint8_t *buf, uint32_t count,
                              struct ipc_port *reply)
{
    int ret = -1;

    if (!vfs_port || !buf || count == 0 || !reply)
        return -1;

    vfs_read_msg_t req;
    kmemset(&req, 0, sizeof(req));
    req.hdr.msgh_size = sizeof(req);
    req.hdr.msgh_id   = VFS_MSG_READ;
    req.fd            = fd;
    req.count         = count;
    req.offset        = offset;
    req.reply_port    = (uint64_t)reply;

    mach_msg_return_t send_mr = ipc_mqueue_send(vfs_port->ip_messages, &req, sizeof(req));
    if (send_mr != MACH_MSG_SUCCESS) {
        serial_putstr("[bsd-vfs-read] ERROR: send failed, mr=");
        serial_puthex((uint64_t)send_mr);
        serial_putstr("\r\n");
        return -1;
    }

    vfs_reply_msg_t rep;
    mach_msg_size_t out_size = 0;
    int got_reply = 0;
    for (int spins = 0; spins < 200000; spins++) {
        mach_msg_return_t mr = ipc_mqueue_receive(
            reply->ip_messages, &rep, sizeof(rep), &out_size, 0 /* non-blocking */);
        if (mr == MACH_MSG_SUCCESS) {
            got_reply = 1;
            break;
        }
        if ((spins % 256) == 0)
            sched_yield();
    }
    if (!got_reply) {
        serial_putstr("[bsd-vfs-read] ERROR: timed out waiting for READ reply\r\n");
        goto out;
    }

    if (rep.retcode != VFS_SUCCESS || rep.result < 0) {
        serial_putstr("[bsd-vfs-read] ERROR: bad reply retcode=");
        serial_puthex((uint64_t)rep.retcode);
        serial_putstr(" result=");
        serial_puthex((uint64_t)rep.result);
        serial_putstr("\r\n");
        goto out;
    }

    if (rep.result > 0)
        kmemcpy(buf, rep.data, (uint32_t)rep.result);

    ret = rep.result;

out:
    return ret;
}

static int bsd_vfs_read_all(const char *path, uint8_t *dst, uint32_t dst_cap, uint32_t *size_out)
{
    struct ipc_port *reply = ipc_port_alloc(kernel_task_ptr());
    if (!reply) {
        serial_putstr("[bsd-vfs] ERROR: reply port alloc failed\r\n");
        return -1;
    }

    serial_putstr("[bsd-vfs] opening: ");
    serial_putstr(path);
    serial_putstr("\r\n");

    int fd = bsd_vfs_open(path, reply);
    if (fd < 0) {
        serial_putstr("[bsd-vfs] ERROR: open failed\r\n");
        ipc_port_destroy(reply);
        return -1;
    }

    serial_putstr("[bsd-vfs] open succeeded, fd=");
    serial_puthex((uint64_t)fd);
    serial_putstr("\r\n");

    uint32_t off = 0;
    for (;;) {
        uint32_t want = VFS_DATA_MAX;
        if (off + want > dst_cap)
            want = dst_cap - off;
        if (want == 0) {
            serial_putstr("[bsd-vfs] ERROR: want=0 (buffer full?)\r\n");
            ipc_port_destroy(reply);
            return -1;
        }

        serial_putstr("[bsd-vfs] reading chunk at offset=");
        serial_puthex((uint64_t)off);
        serial_putstr(" want=");
        serial_puthex((uint64_t)want);
        serial_putstr("\r\n");

        int n = bsd_vfs_read_chunk(fd, off, dst + off, want, reply);
        if (n < 0) {
            serial_putstr("[bsd-vfs] ERROR: read chunk failed\r\n");
            ipc_port_destroy(reply);
            return -1;
        }
        if (n == 0) {
            serial_putstr("[bsd-vfs] EOF reached\r\n");
            break;
        }

        serial_putstr("[bsd-vfs] read ");
        serial_puthex((uint64_t)n);
        serial_putstr(" bytes\r\n");
        off += (uint32_t)n;
    }

    serial_putstr("[bsd-vfs] total read: ");
    serial_puthex((uint64_t)off);
    serial_putstr(" bytes\r\n");

    if (size_out)
        *size_out = off;

    ipc_port_destroy(reply);
    return 0;
}

int bsd_exec_current(const char *path, struct interrupt_frame *frame)
{
    if (!path || !frame)
        return -1;

    serial_putstr("[bsd-exec] starting exec: ");
    serial_putstr(path);
    serial_putstr("\r\n");

    struct thread *cur_th = sched_current();
    if (!cur_th || !cur_th->th_task) {
        serial_putstr("[bsd-exec] ERROR: no current thread/task\r\n");
        return -1;
    }

    struct task *task = cur_th->th_task;
    const uint8_t *image = (const uint8_t *)0;
    uint32_t image_size = 0;

    /* Fast path for init image exported from Multiboot module. */
    if (kstrcmp(path, "/bin/init.elf") == 0 &&
        g_boot_initrd_data && g_boot_initrd_size > 0 &&
        g_boot_initrd_size <= BSD_EXEC_MAX_IMAGE) {
        image = g_boot_initrd_data;
        image_size = (uint32_t)g_boot_initrd_size;
        serial_putstr("[bsd-exec] using boot initrd image\r\n");
    } else {
        uint8_t *image_buf = (uint8_t *)kalloc(BSD_EXEC_MAX_IMAGE);
        if (!image_buf) {
            serial_putstr("[bsd-exec] ERROR: image allocation failed\r\n");
            return -1;
        }

        serial_putstr("[bsd-exec] reading image from VFS\r\n");
        if (bsd_vfs_read_all(path, image_buf, BSD_EXEC_MAX_IMAGE, &image_size) != 0) {
            serial_putstr("[bsd-exec] ERROR: VFS read failed\r\n");
            return -1;
        }

        image = image_buf;
    }

    serial_putstr("[bsd-exec] image size: ");
    serial_puthex((uint64_t)image_size);
    serial_putstr("\r\n");

    uint64_t new_cr3 = paging_create_task_pml4();
    if (!new_cr3) {
        serial_putstr("[bsd-exec] ERROR: paging_create_task_pml4 failed\r\n");
        return -1;
    }

    struct vm_map *new_map = vm_map_create(0, 0xFFFFFFFF80000000ULL);
    if (!new_map) {
        serial_putstr("[bsd-exec] ERROR: vm_map_create failed\r\n");
        return -1;
    }
    new_map->pml4 = (uint64_t *)new_cr3;

    struct vm_map *old_map = task->t_map;
    uint64_t old_cr3 = task->t_cr3;
    task->t_map = new_map;
    task->t_cr3 = new_cr3;

    serial_putstr("[bsd-exec] loading ELF\r\n");
    uint64_t entry = 0;
    if (elf_load(task, image, image_size, &entry) != KERN_SUCCESS) {
        serial_putstr("[bsd-exec] ERROR: elf_load failed\r\n");
        task->t_map = old_map;
        task->t_cr3 = old_cr3;
        return -1;
    }

    serial_putstr("[bsd-exec] mapping user stack\r\n");
    if (vm_map_enter(task->t_map, BSD_USER_STACK_BASE, BSD_USER_STACK_SIZE,
                     VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) {
        serial_putstr("[bsd-exec] ERROR: vm_map_enter for stack failed\r\n");
        task->t_map = old_map;
        task->t_cr3 = old_cr3;
        return -1;
    }

    uint64_t user_rsp = (BSD_USER_STACK_BASE + BSD_USER_STACK_SIZE) - 16;

    frame->rip = entry;
    frame->rsp = user_rsp;
    frame->rax = 0;

    serial_putstr("[bsd-exec] switching address space\r\n");
    __asm__ volatile ("movq %0, %%cr3" : : "r"(new_cr3) : "memory");

    serial_putstr("[bsd-exec] exec complete\r\n");

    (void)old_map;
    (void)old_cr3;
    return 0;
}

/* -------------------------------------------------------------------------
 * bsd_server_main — BSD server thread entry point
 * ------------------------------------------------------------------------- */

void bsd_server_main(void)
{
    serial_putstr("[bsd] initialising BSD server\r\n");

    bsd_port = ipc_port_alloc(kernel_task_ptr());
    if (!bsd_port) {
        serial_putstr("[bsd] FATAL: could not allocate BSD port\r\n");
        for (;;)
            __asm__ volatile ("hlt");
    }

    /* Wait for the bootstrap server to be ready */
    while (!bootstrap_port)
        for (volatile int i = 0; i < 10000; i++)
            ;

    /* Look up the VFS server port */
    bootstrap_lookup_msg_t lookup;
    kmemset(&lookup, 0, sizeof(lookup));
    lookup.hdr.msgh_size = sizeof(lookup);
    lookup.hdr.msgh_id   = BOOTSTRAP_MSG_LOOKUP;
    kstrncpy(lookup.name, "com.unhox.vfs", BOOTSTRAP_NAME_MAX);

    struct ipc_port *vfs_reply = ipc_port_alloc(kernel_task_ptr());
    lookup.reply_port = (uint64_t)vfs_reply;
    ipc_mqueue_send(bootstrap_port->ip_messages, &lookup, sizeof(lookup));

    bootstrap_reply_msg_t vfs_rep;
    mach_msg_size_t vfs_rep_size = 0;
    ipc_mqueue_receive(vfs_reply->ip_messages, &vfs_rep, sizeof(vfs_rep), &vfs_rep_size, 1);

    if (vfs_rep.retcode == BOOTSTRAP_SUCCESS && vfs_rep.port_val != 0) {
        vfs_port = (struct ipc_port *)vfs_rep.port_val;
        serial_putstr("[bsd] VFS port resolved\r\n");
    } else {
        serial_putstr("[bsd] WARNING: could not resolve VFS port\r\n");
    }

    /* Register as "com.unhox.bsd" */
    bootstrap_register_msg_t reg;
    kmemset(&reg, 0, sizeof(reg));
    reg.hdr.msgh_size = sizeof(reg);
    reg.hdr.msgh_id   = BOOTSTRAP_MSG_REGISTER;
    kstrncpy(reg.name, "com.unhox.bsd", BOOTSTRAP_NAME_MAX);
    reg.service_port  = (uint64_t)bsd_port;
    ipc_mqueue_send(bootstrap_port->ip_messages, &reg, sizeof(reg));

    serial_putstr("[bsd] BSD server ready\r\n");

    /*
     * Message receive loop.
     * Buffer sized for the largest request (bsd_write_msg_t).
     */
    uint8_t buf[sizeof(bsd_write_msg_t) + 16];

    for (;;) {
        mach_msg_size_t msg_size = 0;
        mach_msg_return_t mr = ipc_mqueue_receive(
            bsd_port->ip_messages,
            buf, sizeof(buf),
            &msg_size,
            1 /* blocking */);

        if (mr != MACH_MSG_SUCCESS)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

        switch (hdr->msgh_id) {

        case BSD_MSG_WRITE: {
            if (msg_size < sizeof(mach_msg_header_t) + 2 * sizeof(uint32_t))
                break;
            bsd_write_msg_t *req = (bsd_write_msg_t *)buf;

            /* fd 1 and fd 2 → serial output */
            if (req->fd == 1 || req->fd == 2) {
                uint32_t n = req->count < BSD_DATA_MAX ? req->count : BSD_DATA_MAX;
                for (uint32_t i = 0; i < n; i++)
                    serial_putchar((char)req->data[i]);
            }
            break;
        }

        case BSD_MSG_READ: {
            if (msg_size < sizeof(bsd_read_msg_t))
                break;
            bsd_read_msg_t *req = (bsd_read_msg_t *)buf;

            bsd_reply_msg_t reply;
            kmemset(&reply, 0, sizeof(reply));
            reply.hdr.msgh_size = sizeof(reply);
            reply.hdr.msgh_id   = BSD_MSG_REPLY;

            /* fd 0 → serial input (non-blocking) */
            if (req->fd == 0) {
                char c = serial_getchar();
                if (c != 0) {
                    reply.data[0] = (uint8_t)c;
                    reply.count   = 1;
                } else {
                    reply.count   = 0;
                }
                reply.retcode = BSD_SUCCESS;
            } else {
                reply.retcode = BSD_BAD_FD;
            }

            if (req->reply_port) {
                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
            }
            break;
        }

        default:
            break;
        }
    }
}
