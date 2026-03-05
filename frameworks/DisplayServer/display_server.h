/*
 * frameworks/DisplayServer/display_server.h — UNHOX Display Server interface
 *
 * DPS-inspired compositing server using VGA text mode (80x25, 16 colors).
 * Runs as a kernel thread, receives drawing commands via Mach IPC,
 * composites to the VGA text buffer.
 */

#ifndef DISPLAY_SERVER_H
#define DISPLAY_SERVER_H

#include <stdint.h>
#include "dps_msg.h"

/* Window record */
typedef struct {
    int      active;
    uint8_t  x, y, w, h;               /* character-cell position and size */
    char     title[DPS_TITLE_MAX];
    uint8_t  fg, bg;                    /* default colors */
    /* Per-window content buffer (content area = inside the frame) */
    char     content[23][78];           /* max content rows x cols */
    uint8_t  cfgmap[23][78];            /* foreground color per cell */
    uint8_t  cbgmap[23][78];            /* background color per cell */
} dps_window_t;

/* Compositor API (internal) */
void compositor_init(void);
void compositor_redraw_all(void);

/* Server thread entry point — call from kern.c */
void display_server_main(void);

#endif /* DISPLAY_SERVER_H */
