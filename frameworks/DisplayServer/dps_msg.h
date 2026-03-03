/*
 * frameworks/DisplayServer/dps_msg.h — Display Server IPC message definitions
 *
 * The UNHOX Display Server runs as a kernel thread registered as
 * "com.unhox.display" with the bootstrap server.  It provides a
 * Display PostScript-inspired compositing surface over the kernel
 * framebuffer, using Mach port IPC natively.
 *
 * Message flow:
 *   client → DPS_MSG_WINDOW_CREATE  → display_server → DPS_MSG_REPLY (wid)
 *   client → DPS_MSG_DRAW_RECT      → display_server (fire-and-forget)
 *   client → DPS_MSG_DRAW_TEXT      → display_server (fire-and-forget)
 *   client → DPS_MSG_FLUSH          → display_server (fire-and-forget)
 *   client → DPS_MSG_WINDOW_DESTROY → display_server (fire-and-forget)
 *
 * Reference: NeXTSTEP Display PostScript documentation (archive/next-docs/);
 *            DPS Concepts (Adobe Systems 1991), §2 — Window architecture.
 */

#ifndef DPS_MSG_H
#define DPS_MSG_H

#include <stdint.h>
#include "mach/mach_types.h"

/* Forward declaration */
struct ipc_port;

/* -------------------------------------------------------------------------
 * Display Server message IDs
 * ------------------------------------------------------------------------- */
#define DPS_MSG_WINDOW_CREATE   400     /* create a new window              */
#define DPS_MSG_WINDOW_DESTROY  401     /* destroy an existing window        */
#define DPS_MSG_WINDOW_MOVE     402     /* move a window to new position     */
#define DPS_MSG_WINDOW_RESIZE   403     /* resize a window                   */
#define DPS_MSG_DRAW_RECT       404     /* fill a rectangle with a color     */
#define DPS_MSG_DRAW_TEXT       405     /* draw a text string                */
#define DPS_MSG_FLUSH           406     /* flush pending drawing to screen   */
#define DPS_MSG_REPLY           407     /* reply from display server         */

/* -------------------------------------------------------------------------
 * Limits
 * ------------------------------------------------------------------------- */
#define DPS_TITLE_MAX           64      /* max window title length           */
#define DPS_TEXT_MAX            128     /* max text string per draw message  */
#define DPS_MAX_WINDOWS         32      /* max concurrent windows            */

/* -------------------------------------------------------------------------
 * Color representation — 32-bit ARGB (alpha ignored by compositor v1)
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t r, g, b, a;
} dps_color_t;

#define DPS_COLOR_BLACK     ((dps_color_t){0,   0,   0,   255})
#define DPS_COLOR_WHITE     ((dps_color_t){255, 255, 255, 255})
#define DPS_COLOR_GRAY      ((dps_color_t){128, 128, 128, 255})
#define DPS_COLOR_DARK_GRAY ((dps_color_t){64,  64,  64,  255})
#define DPS_COLOR_LIGHT_GRAY ((dps_color_t){192, 192, 192, 255})
#define DPS_COLOR_BLUE      ((dps_color_t){0,   0,   200, 255})
#define DPS_COLOR_DARK_BLUE ((dps_color_t){0,   0,   128, 255})
#define DPS_COLOR_RED       ((dps_color_t){200, 0,   0,   255})
#define DPS_COLOR_GREEN     ((dps_color_t){0,   180, 0,   255})
#define DPS_COLOR_YELLOW    ((dps_color_t){240, 220, 0,   255})

/* Return codes */
#define DPS_SUCCESS         0
#define DPS_INVALID_WID     1
#define DPS_NO_MEMORY       2
#define DPS_LIMIT_REACHED   3

/* Global display server port — set by display_server_main() */
extern struct ipc_port *display_port;

/* -------------------------------------------------------------------------
 * Message structures
 * ------------------------------------------------------------------------- */

/*
 * dps_window_create_msg_t — request a new window.
 * Server replies with dps_reply_msg_t (result = window id, or < 0 on error).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             x;              /* initial left edge (pixels)        */
    int32_t             y;              /* initial top edge (pixels)         */
    uint32_t            width;          /* window width in pixels            */
    uint32_t            height;         /* window height in pixels           */
    char                title[DPS_TITLE_MAX]; /* window title string         */
    uint64_t            reply_port;     /* ipc_port * for reply              */
} dps_window_create_msg_t;

/*
 * dps_window_destroy_msg_t — destroy a window (fire-and-forget).
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;            /* window id returned by CREATE      */
} dps_window_destroy_msg_t;

/*
 * dps_window_move_msg_t — move a window to a new position.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;
    int32_t             x;
    int32_t             y;
} dps_window_move_msg_t;

/*
 * dps_window_resize_msg_t — resize a window.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;
    uint32_t            width;
    uint32_t            height;
} dps_window_resize_msg_t;

/*
 * dps_draw_rect_msg_t — fill a rectangle within a window.
 *
 * Coordinates are relative to the window's top-left corner.
 * Fire-and-forget; no reply.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;
    int32_t             x;
    int32_t             y;
    uint32_t            width;
    uint32_t            height;
    dps_color_t         color;
} dps_draw_rect_msg_t;

/*
 * dps_draw_text_msg_t — render a text string at a position within a window.
 *
 * The display server uses the VGA font for initial text rendering.
 * Coordinates are relative to the window's top-left corner.
 * Fire-and-forget; no reply.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;
    int32_t             x;
    int32_t             y;
    dps_color_t         fg;             /* foreground color                  */
    dps_color_t         bg;             /* background color                  */
    char                text[DPS_TEXT_MAX]; /* null-terminated string        */
} dps_draw_text_msg_t;

/*
 * dps_flush_msg_t — flush pending drawing for a window to the screen.
 * Fire-and-forget; no reply.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             wid;
} dps_flush_msg_t;

/*
 * dps_reply_msg_t — reply from display server to client.
 */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             retcode;        /* DPS_SUCCESS or error code         */
    int32_t             result;         /* wid on CREATE; 0 otherwise        */
} dps_reply_msg_t;

/* -------------------------------------------------------------------------
 * Server entry point
 * ------------------------------------------------------------------------- */

void display_server_main(void);

#endif /* DPS_MSG_H */
