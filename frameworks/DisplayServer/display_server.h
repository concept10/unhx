/*
 * frameworks/DisplayServer/display_server.h — UNHOX Display Server API
 *
 * The Display Server is the central compositor for the UNHOX desktop.
 * It manages a set of windows composited onto the kernel framebuffer or
 * VGA text surface, and accepts drawing commands from client applications
 * via Mach port IPC.
 *
 * Architecture:
 *   - Runs as a dedicated kernel thread ("display_server_thread")
 *   - Registers "com.unhox.display" with the bootstrap server
 *   - Maintains a window list (max DPS_MAX_WINDOWS entries)
 *   - Draws directly to the VGA text buffer (Phase 5 v1) or linear
 *     framebuffer (Phase 5 v2 when VESA/GOP is available)
 *   - Provides 9px × 16px character cell rendering via the VGA font
 *
 * NeXT design note:
 *   NeXTSTEP's DPS server ran as a separate Mach task receiving drawing
 *   primitives via IPC and rendering to a pixel framebuffer.  UNHOX adopts
 *   the same architecture but targets VGA text mode for Phase 5 v1 to avoid
 *   the VESA initialisation dependency.
 *
 * Reference: NeXTSTEP System Architecture (NeXT, 1990) §4 — Display Server;
 *            OSF MK display server stubs in servers/display/.
 */

#ifndef DISPLAY_SERVER_H
#define DISPLAY_SERVER_H

#include <stdint.h>
#include "dps_msg.h"

/* -------------------------------------------------------------------------
 * Window record
 * ------------------------------------------------------------------------- */

#define DPS_WIN_FLAG_ACTIVE     0x01    /* window is live                    */
#define DPS_WIN_FLAG_VISIBLE    0x02    /* window is on screen               */
#define DPS_WIN_FLAG_FOCUSED    0x04    /* window holds keyboard focus        */

typedef struct {
    int32_t     wid;                    /* window id (index + 1)             */
    uint32_t    flags;                  /* DPS_WIN_FLAG_* bitmask            */
    int32_t     x, y;                  /* screen position of top-left        */
    uint32_t    width, height;          /* dimensions in pixels              */
    char        title[DPS_TITLE_MAX];   /* window title                      */
} dps_window_t;

/* -------------------------------------------------------------------------
 * Compositor state (global, single-threaded access from server loop)
 * ------------------------------------------------------------------------- */

extern dps_window_t dps_windows[DPS_MAX_WINDOWS];
extern int          dps_window_count;

/* -------------------------------------------------------------------------
 * Internal compositor operations
 * ------------------------------------------------------------------------- */

/*
 * compositor_init — initialise the compositor layer.
 * Must be called before any window operations.
 * Clears the display and renders the desktop background.
 */
void compositor_init(void);

/*
 * compositor_redraw_all — repaint the entire screen.
 * Renders desktop background then all visible windows bottom-to-top.
 */
void compositor_redraw_all(void);

/*
 * compositor_draw_window_frame — draw the window chrome for a window.
 * Renders title bar, border, and background fill.
 */
void compositor_draw_window_frame(const dps_window_t *win);

/*
 * compositor_draw_desktop — render the desktop background.
 */
void compositor_draw_desktop(void);

/* -------------------------------------------------------------------------
 * Window management operations
 * ------------------------------------------------------------------------- */

/*
 * dps_window_create — allocate a new window.
 * Returns the window id (> 0) on success, or a negative DPS_* error code.
 */
int32_t dps_window_create(int32_t x, int32_t y,
                           uint32_t width, uint32_t height,
                           const char *title);

/*
 * dps_window_destroy — destroy a window by id.
 * Returns DPS_SUCCESS or DPS_INVALID_WID.
 */
int32_t dps_window_destroy(int32_t wid);

/*
 * dps_window_move — move a window.
 */
int32_t dps_window_move(int32_t wid, int32_t x, int32_t y);

/*
 * dps_window_resize — resize a window.
 */
int32_t dps_window_resize(int32_t wid, uint32_t width, uint32_t height);

/*
 * dps_draw_rect — fill a window-relative rectangle.
 */
int32_t dps_draw_rect(int32_t wid, int32_t x, int32_t y,
                      uint32_t w, uint32_t h, dps_color_t color);

/*
 * dps_draw_text — draw a text string within a window.
 */
int32_t dps_draw_text(int32_t wid, int32_t x, int32_t y,
                      dps_color_t fg, dps_color_t bg,
                      const char *text);

#endif /* DISPLAY_SERVER_H */
