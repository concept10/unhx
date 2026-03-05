/*
 * frameworks/AppKit-backend/appkit_backend.c — AppKit display server backend
 *
 * Sends DPS Mach IPC messages to the UNHOX Display Server.
 * Used by the workspace manager and any kernel-space client that needs
 * window management.
 */

#include "appkit_backend.h"
#include "DisplayServer/dps_msg.h"
#include "kern/klib.h"
#include "kern/task.h"
#include "kern/sched.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "bootstrap/bootstrap.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);

/* Cached display server port */
static struct ipc_port *dp_port;

int appkit_backend_init(void)
{
    /* Wait for bootstrap */
    while (!bootstrap_port)
        sched_yield();

    /* Lookup "com.unhox.display" via bootstrap */
    struct task *ktask = kernel_task_ptr();
    struct ipc_port *reply_port = ipc_port_alloc(ktask);
    if (!reply_port)
        return -1;

    bootstrap_lookup_msg_t lkup;
    kmemset(&lkup, 0, sizeof(lkup));
    lkup.hdr.msgh_size = sizeof(lkup);
    lkup.hdr.msgh_id = BOOTSTRAP_MSG_LOOKUP;
    kstrncpy(lkup.name, "com.unhox.display", BOOTSTRAP_NAME_MAX);
    lkup.reply_port = (uint64_t)reply_port;

    ipc_mqueue_send(bootstrap_port->ip_messages, &lkup, sizeof(lkup));

    /* Wait for reply */
    uint8_t rbuf[sizeof(bootstrap_reply_msg_t)];
    mach_msg_size_t out_size = 0;
    mach_msg_return_t mr;
    do {
        mr = ipc_mqueue_receive(reply_port->ip_messages, rbuf, sizeof(rbuf),
                                &out_size, 0);
        if (mr != MACH_MSG_SUCCESS)
            sched_yield();
    } while (mr != MACH_MSG_SUCCESS);

    bootstrap_reply_msg_t *rep = (bootstrap_reply_msg_t *)rbuf;
    if (rep->retcode != BOOTSTRAP_SUCCESS || !rep->port_val) {
        serial_putstr("[appkit] FAIL — display server not found\r\n");
        return -1;
    }

    dp_port = (struct ipc_port *)(uintptr_t)rep->port_val;
    return 0;
}

int appkit_window_create(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                         const char *title, uint8_t fg, uint8_t bg)
{
    if (!dp_port) return -1;

    struct task *ktask = kernel_task_ptr();
    struct ipc_port *reply_port = ipc_port_alloc(ktask);
    if (!reply_port) return -1;

    dps_window_create_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_WINDOW_CREATE;
    msg.reply_port = (uint64_t)reply_port;
    msg.x = x;
    msg.y = y;
    msg.w = w;
    msg.h = h;
    msg.fg = fg;
    msg.bg = bg;
    if (title)
        kstrncpy(msg.title, title, DPS_TITLE_MAX - 1);

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));

    /* Block on reply */
    uint8_t rbuf[sizeof(dps_reply_msg_t)];
    mach_msg_size_t out_size = 0;
    mach_msg_return_t mr = ipc_mqueue_receive(
        reply_port->ip_messages, rbuf, sizeof(rbuf), &out_size, 1);

    if (mr != MACH_MSG_SUCCESS)
        return -1;

    dps_reply_msg_t *rep = (dps_reply_msg_t *)rbuf;
    return (rep->retcode == 0) ? rep->window_id : -1;
}

void appkit_window_destroy(int window_id)
{
    if (!dp_port) return;

    dps_window_destroy_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_WINDOW_DESTROY;
    msg.window_id = window_id;

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));
}

void appkit_window_move(int window_id, uint8_t x, uint8_t y)
{
    if (!dp_port) return;

    dps_window_move_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_WINDOW_MOVE;
    msg.window_id = window_id;
    msg.x = x;
    msg.y = y;

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));
}

void appkit_draw_rect(int window_id, uint8_t x, uint8_t y,
                      uint8_t w, uint8_t h, uint8_t fg, uint8_t bg)
{
    if (!dp_port) return;

    dps_draw_rect_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_DRAW_RECT;
    msg.window_id = window_id;
    msg.x = x;
    msg.y = y;
    msg.w = w;
    msg.h = h;
    msg.fg = fg;
    msg.bg = bg;

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));
}

void appkit_draw_text(int window_id, uint8_t x, uint8_t y,
                      const char *text, uint8_t fg, uint8_t bg)
{
    if (!dp_port) return;

    dps_draw_text_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_DRAW_TEXT;
    msg.window_id = window_id;
    msg.x = x;
    msg.y = y;
    msg.fg = fg;
    msg.bg = bg;
    if (text)
        kstrncpy(msg.text, text, DPS_TEXT_MAX - 1);

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));
}

void appkit_flush(void)
{
    if (!dp_port) return;

    dps_flush_msg_t msg;
    kmemset(&msg, 0, sizeof(msg));
    msg.hdr.msgh_size = sizeof(msg);
    msg.hdr.msgh_id = DPS_MSG_FLUSH;

    ipc_mqueue_send(dp_port->ip_messages, &msg, sizeof(msg));
}
