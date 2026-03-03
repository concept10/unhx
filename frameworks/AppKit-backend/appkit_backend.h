/*
 * frameworks/AppKit-backend/appkit_backend.h — AppKit Display Server backend for UNHOX
 *
 * This is the UNHOX-native backend for the GNUstep AppKit (libs-gui).
 * It replaces the X11/Cairo backends with a direct connection to the UNHOX
 * Display Server via Mach port IPC.
 *
 * Architecture:
 *   AppKit NSWindow → appkit_backend → Mach IPC → Display Server → screen
 *
 * In Phase 5 v1 this backend runs inside the kernel address space (alongside
 * the display server itself) as a library usable by kernel-linked applications
 * and the workspace manager.  When the UNHOX userspace ABI is established
 * (Phase 6) this header will be exposed as a userspace framework API.
 *
 * Design note:
 *   GNUstep's backend architecture (GSBackend protocol) defines the operations
 *   a backend must implement to support NSWindow, NSView, and event delivery.
 *   We provide a minimal subset sufficient for Phase 5 milestone v1.0.
 *
 * Reference: GNUstep Back README; GNUstep libs-gui GSDisplayServer.h;
 *            NeXTSTEP AppKit architecture (NeXT developer docs, 1993).
 */

#ifndef APPKIT_BACKEND_H
#define APPKIT_BACKEND_H

#include <stdint.h>
#include "frameworks/DisplayServer/dps_msg.h"

/* -------------------------------------------------------------------------
 * Opaque AppKit window handle
 * ------------------------------------------------------------------------- */

typedef struct appkit_window appkit_window_t;

/* -------------------------------------------------------------------------
 * Backend lifecycle
 * ------------------------------------------------------------------------- */

/*
 * appkit_backend_init — connect to the display server.
 *
 * Looks up "com.unhox.display" via the bootstrap server and stores the
 * send-right port for subsequent operations.
 *
 * Returns 0 on success, -1 if the display server is not available.
 */
int appkit_backend_init(void);

/* -------------------------------------------------------------------------
 * Window operations
 * ------------------------------------------------------------------------- */

/*
 * appkit_window_create — create a new application window.
 *
 * x, y:        screen position (pixels from top-left of desktop)
 * width/height: window dimensions in pixels
 * title:       UTF-8 window title (max DPS_TITLE_MAX-1 chars)
 *
 * Returns an opaque window handle on success, or NULL on failure.
 */
appkit_window_t *appkit_window_create(int32_t x, int32_t y,
                                      uint32_t width, uint32_t height,
                                      const char *title);

/*
 * appkit_window_destroy — destroy a window and release its resources.
 */
void appkit_window_destroy(appkit_window_t *win);

/*
 * appkit_window_move — move a window to a new screen position.
 */
void appkit_window_move(appkit_window_t *win, int32_t x, int32_t y);

/*
 * appkit_window_resize — resize a window.
 */
void appkit_window_resize(appkit_window_t *win,
                           uint32_t width, uint32_t height);

/* -------------------------------------------------------------------------
 * Drawing operations (AppKit NSView content)
 * All coordinates are relative to the window's top-left content corner.
 * ------------------------------------------------------------------------- */

/*
 * appkit_draw_rect — fill a rectangle with a solid color.
 */
void appkit_draw_rect(appkit_window_t *win,
                      int32_t x, int32_t y,
                      uint32_t width, uint32_t height,
                      dps_color_t color);

/*
 * appkit_draw_text — draw a text string.
 */
void appkit_draw_text(appkit_window_t *win,
                      int32_t x, int32_t y,
                      dps_color_t fg, dps_color_t bg,
                      const char *text);

/*
 * appkit_flush — flush pending drawing operations to the screen.
 */
void appkit_flush(appkit_window_t *win);

/* -------------------------------------------------------------------------
 * Introspection
 * ------------------------------------------------------------------------- */

/*
 * appkit_window_id — return the display server window id for this handle.
 * Returns 0 if the window is invalid.
 */
int32_t appkit_window_id(const appkit_window_t *win);

#endif /* APPKIT_BACKEND_H */
