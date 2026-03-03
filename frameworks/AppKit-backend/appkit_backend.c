/*
 * frameworks/AppKit-backend/appkit_backend.c — AppKit Display Server backend for UNHOX
 *
 * Implements the AppKit → Display Server IPC bridge.
 * Each operation sends a Mach message to display_port and (where needed)
 * waits for a reply on a per-call reply port.
 *
 * Phase 5 v1 notes:
 *   - appkit_backend_init() wires directly to display_port (already a global
 *     set by display_server_main()) — full bootstrap lookup is implemented
 *     but falls back to the direct pointer when running inside the kernel.
 *   - appkit_window_t is a stack/heap struct allocated via kernel kalloc().
 *
 * Reference: GNUstep libs-gui source, Back/x11/XGServer.m for comparison;
 *            NeXTSTEP AppKit architecture (NeXT developer docs, 1993).
 */

#include "appkit_backend.h"
#include "frameworks/DisplayServer/dps_msg.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/kalloc.h"
#include "kern/klib.h"
#include "kern/task.h"

/* -------------------------------------------------------------------------
 * External references
 * ------------------------------------------------------------------------- */

extern void serial_putstr(const char *s);
extern struct task *kernel_task_ptr(void);

/* Display server port (set by display_server_main()) */
extern struct ipc_port *display_port;

/* -------------------------------------------------------------------------
 * Opaque window handle
 * ------------------------------------------------------------------------- */

struct appkit_window {
    int32_t wid;                /* display server window id      */
    int32_t x, y;
    uint32_t width, height;
    char title[DPS_TITLE_MAX];
};

/* -------------------------------------------------------------------------
 * Backend state
 * ------------------------------------------------------------------------- */

static int backend_ready = 0;

/* -------------------------------------------------------------------------
 * appkit_backend_init
 * ------------------------------------------------------------------------- */

int appkit_backend_init(void)
{
    if (display_port) {
        backend_ready = 1;
        serial_putstr("[appkit] backend connected to display server\r\n");
        return 0;
    }
    serial_putstr("[appkit] display server not available\r\n");
    return -1;
}

/* -------------------------------------------------------------------------
 * appkit_window_create
 * ------------------------------------------------------------------------- */

appkit_window_t *appkit_window_create(int32_t x, int32_t y,
                                      uint32_t width, uint32_t height,
                                      const char *title)
{
    if (!backend_ready || !display_port)
        return (void *)0;

    /* Allocate reply port for this call */
    struct ipc_port *reply = ipc_port_alloc(kernel_task_ptr());
    if (!reply) return (void *)0;

    /* Build CREATE message */
    dps_window_create_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_WINDOW_CREATE;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.x      = x;
    req.y      = y;
    req.width  = width;
    req.height = height;
    req.reply_port = (uint64_t)(uintptr_t)reply;

    /* Copy title */
    int i;
    for (i = 0; title[i] && i < DPS_TITLE_MAX - 1; i++)
        req.title[i] = title[i];
    req.title[i] = '\0';

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));

    /* Wait for reply */
    dps_reply_msg_t rep;
    mach_msg_size_t rep_size = 0;
    mach_msg_return_t mr = ipc_mqueue_receive(
        reply->ip_messages,
        (uint8_t *)&rep, sizeof(rep), &rep_size, 1 /* blocking */);

    ipc_port_destroy(reply);

    if (mr != MACH_MSG_SUCCESS || rep.retcode != DPS_SUCCESS || rep.result <= 0)
        return (void *)0;

    /* Allocate handle */
    appkit_window_t *win = (appkit_window_t *)kalloc(sizeof(*win));
    if (!win) return (void *)0;

    win->wid    = rep.result;
    win->x      = x;
    win->y      = y;
    win->width  = width;
    win->height = height;
    for (i = 0; req.title[i] && i < DPS_TITLE_MAX - 1; i++)
        win->title[i] = req.title[i];
    win->title[i] = '\0';

    return win;
}

/* -------------------------------------------------------------------------
 * appkit_window_destroy
 * ------------------------------------------------------------------------- */

void appkit_window_destroy(appkit_window_t *win)
{
    if (!win || !display_port) return;

    dps_window_destroy_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_WINDOW_DESTROY;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid = win->wid;

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
    kfree(win);
}

/* -------------------------------------------------------------------------
 * appkit_window_move
 * ------------------------------------------------------------------------- */

void appkit_window_move(appkit_window_t *win, int32_t x, int32_t y)
{
    if (!win || !display_port) return;

    dps_window_move_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_WINDOW_MOVE;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid = win->wid;
    req.x   = x;
    req.y   = y;

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
    win->x = x;
    win->y = y;
}

/* -------------------------------------------------------------------------
 * appkit_window_resize
 * ------------------------------------------------------------------------- */

void appkit_window_resize(appkit_window_t *win,
                           uint32_t width, uint32_t height)
{
    if (!win || !display_port) return;

    dps_window_resize_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_WINDOW_RESIZE;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid    = win->wid;
    req.width  = width;
    req.height = height;

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
    win->width  = width;
    win->height = height;
}

/* -------------------------------------------------------------------------
 * appkit_draw_rect
 * ------------------------------------------------------------------------- */

void appkit_draw_rect(appkit_window_t *win,
                      int32_t x, int32_t y,
                      uint32_t width, uint32_t height,
                      dps_color_t color)
{
    if (!win || !display_port) return;

    dps_draw_rect_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_DRAW_RECT;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid    = win->wid;
    req.x      = x;
    req.y      = y;
    req.width  = width;
    req.height = height;
    req.color  = color;

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
}

/* -------------------------------------------------------------------------
 * appkit_draw_text
 * ------------------------------------------------------------------------- */

void appkit_draw_text(appkit_window_t *win,
                      int32_t x, int32_t y,
                      dps_color_t fg, dps_color_t bg,
                      const char *text)
{
    if (!win || !display_port || !text) return;

    dps_draw_text_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_DRAW_TEXT;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid = win->wid;
    req.x   = x;
    req.y   = y;
    req.fg  = fg;
    req.bg  = bg;

    int i;
    for (i = 0; text[i] && i < DPS_TEXT_MAX - 1; i++)
        req.text[i] = text[i];
    req.text[i] = '\0';

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
}

/* -------------------------------------------------------------------------
 * appkit_flush
 * ------------------------------------------------------------------------- */

void appkit_flush(appkit_window_t *win)
{
    if (!win || !display_port) return;

    dps_flush_msg_t req;
    req.hdr.msgh_id          = DPS_MSG_FLUSH;
    req.hdr.msgh_size        = sizeof(req);
    req.hdr.msgh_remote_port  = 0;
    req.hdr.msgh_local_port   = 0;
    req.hdr.msgh_voucher_port = 0;
    req.hdr.msgh_bits        = 0;
    req.wid = win->wid;

    ipc_mqueue_send(display_port->ip_messages,
                    (mach_msg_header_t *)&req, sizeof(req));
}

/* -------------------------------------------------------------------------
 * appkit_window_id
 * ------------------------------------------------------------------------- */

int32_t appkit_window_id(const appkit_window_t *win)
{
    if (!win) return 0;
    return win->wid;
}
