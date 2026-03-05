/*
 * frameworks/DisplayServer/dps_msg.h — DPS message protocol for UNHOX
 *
 * Defines the Mach IPC message types exchanged between AppKit clients
 * and the UNHOX Display Server (DPS-inspired compositor).
 *
 * All messages start with mach_msg_header_t. The msgh_id field selects
 * the operation. Port values carried as uint64_t (kernel pointers).
 *
 * Coordinates are VGA text-mode character cells (0-79 x, 0-24 y).
 * Colors are VGA 4-bit indices (0-15).
 */

#ifndef DPS_MSG_H
#define DPS_MSG_H

#include <stdint.h>
#include "mach/mach_types.h"

/* Limits */
#define DPS_MAX_WINDOWS     16
#define DPS_TITLE_MAX       40
#define DPS_TEXT_MAX         76

/* -------------------------------------------------------------------------
 * DPS message IDs (400-407)
 * ------------------------------------------------------------------------- */

#define DPS_MSG_WINDOW_CREATE   400
#define DPS_MSG_WINDOW_DESTROY  401
#define DPS_MSG_WINDOW_MOVE     402
#define DPS_MSG_WINDOW_RESIZE   403
#define DPS_MSG_DRAW_RECT       404
#define DPS_MSG_DRAW_TEXT       405
#define DPS_MSG_FLUSH           406
#define DPS_MSG_REPLY           407
#define DPS_MSG_MOUSE_EVENT     408

/* -------------------------------------------------------------------------
 * DPS message structures
 * ------------------------------------------------------------------------- */

/* Create a window: (x, y, w, h, title) → reply with window_id */
typedef struct {
    mach_msg_header_t   hdr;
    uint64_t            reply_port;     /* ipc_port * for reply */
    uint8_t             x, y, w, h;     /* character-cell coords */
    uint8_t             fg, bg;         /* default text colors */
    uint8_t             _pad[2];
    char                title[DPS_TITLE_MAX];
} dps_window_create_msg_t;

/* Destroy a window by id */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             window_id;
    uint32_t            _pad;
} dps_window_destroy_msg_t;

/* Move a window to new position */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             window_id;
    uint8_t             x, y;
    uint8_t             _pad[2];
} dps_window_move_msg_t;

/* Resize a window */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             window_id;
    uint8_t             w, h;
    uint8_t             _pad[2];
} dps_window_resize_msg_t;

/* Fill a rectangle in a window with a color */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             window_id;
    uint8_t             x, y, w, h;     /* relative to window content area */
    uint8_t             fg, bg;
    uint8_t             _pad[2];
} dps_draw_rect_msg_t;

/* Draw text at position in window */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             window_id;
    uint8_t             x, y;           /* relative to window content area */
    uint8_t             fg, bg;
    char                text[DPS_TEXT_MAX];
} dps_draw_text_msg_t;

/* Flush: redraw all dirty windows to VGA buffer */
typedef struct {
    mach_msg_header_t   hdr;
} dps_flush_msg_t;

/* Reply message (returned for WINDOW_CREATE) */
typedef struct {
    mach_msg_header_t   hdr;
    int32_t             retcode;        /* 0 = success, -1 = error */
    int32_t             window_id;      /* assigned window id on success */
} dps_reply_msg_t;

/* Mouse event — sent internally by display server's input thread */
typedef struct {
    mach_msg_header_t   hdr;
    int16_t             dx, dy;         /* relative motion */
    uint8_t             buttons;        /* MOUSE_BTN_* bitmask */
    uint8_t             _pad[3];
} dps_mouse_event_msg_t;

#endif /* DPS_MSG_H */
