/*
 * frameworks/AppKit-backend/appkit_backend.h — AppKit display server backend
 *
 * C library that bridges window management calls to the UNHOX Display Server
 * via Mach IPC messages. Linked into programs (workspace manager, etc.)
 * that need windowing.
 */

#ifndef APPKIT_BACKEND_H
#define APPKIT_BACKEND_H

#include <stdint.h>

/*
 * appkit_backend_init — look up "com.unhox.display" via bootstrap.
 * Must be called before any other appkit_* function.
 * Returns 0 on success, -1 if display server not found.
 */
int appkit_backend_init(void);

/*
 * appkit_window_create — create a window on the display server.
 * Coordinates are VGA character cells. Returns window_id (>= 0) or -1.
 */
int appkit_window_create(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                         const char *title, uint8_t fg, uint8_t bg);

/* Destroy a window */
void appkit_window_destroy(int window_id);

/* Move a window to new position */
void appkit_window_move(int window_id, uint8_t x, uint8_t y);

/* Draw filled rectangle in window content area */
void appkit_draw_rect(int window_id, uint8_t x, uint8_t y,
                      uint8_t w, uint8_t h, uint8_t fg, uint8_t bg);

/* Draw text in window content area */
void appkit_draw_text(int window_id, uint8_t x, uint8_t y,
                      const char *text, uint8_t fg, uint8_t bg);

/* Flush: trigger compositor redraw of all windows */
void appkit_flush(void);

#endif /* APPKIT_BACKEND_H */
