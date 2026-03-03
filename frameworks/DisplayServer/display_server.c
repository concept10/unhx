/*
 * frameworks/DisplayServer/display_server.c — UNHOX Display Server
 *
 * Phase 5 v1 implementation:
 *   - Runs as a dedicated kernel thread ("display_server_thread")
 *   - Registers "com.unhox.display" with the bootstrap server
 *   - Manages up to DPS_MAX_WINDOWS windows
 *   - Renders to VGA text buffer (80×25) using box-drawing characters
 *   - Accepts Mach IPC messages for window create/destroy/draw operations
 *
 * VGA text-mode rendering notes:
 *   In Phase 5 v1 we target VGA text mode (80×25 cells) because it is
 *   always available with no bootloader cooperation.  Each window is
 *   mapped to a rectangular region of the text grid; the title bar occupies
 *   the first row of the cell region and the content area the remaining rows.
 *
 *   Character cell coordinates in the APIs below are in pixels (8×16 per
 *   cell).  The compositor converts pixel coordinates to cell coordinates by
 *   dividing by the cell dimensions (CELL_W=8, CELL_H=16).
 *
 * Reference: NeXTSTEP System Architecture (NeXT, 1990) §4 — Display Server.
 */

#include "display_server.h"
#include "dps_msg.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "kern/klib.h"
#include "kern/task.h"
#include "servers/bootstrap/bootstrap.h"
#include "device/vga_text.h"

/* External symbols */
extern void serial_putstr(const char *s);
extern struct ipc_port *bootstrap_port;
extern struct task *kernel_task_ptr(void);

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

struct ipc_port *display_port = (void *)0;

dps_window_t dps_windows[DPS_MAX_WINDOWS];
int          dps_window_count = 0;

/* -------------------------------------------------------------------------
 * VGA text-mode geometry
 *
 * Screen:  80 columns × 25 rows
 * Cell:    8 px wide × 16 px tall
 * Desktop: VGA text grid mapped 1:1 to cell coordinates.
 * -------------------------------------------------------------------------
 */
#define SCREEN_COLS     80
#define SCREEN_ROWS     25
#define CELL_W          8
#define CELL_H          16

/* Convert pixel coordinate to VGA column/row */
#define PX_TO_COL(px)   ((uint8_t)((px) / CELL_W))
#define PX_TO_ROW(px)   ((uint8_t)((px) / CELL_H))

/* dps_color_t → vga_color_t approximation */
static vga_color_t dps_to_vga_color(dps_color_t c)
{
    /* Pick the closest VGA color by dominant channel */
    if (c.r < 64 && c.g < 64 && c.b < 64)  return VGA_COLOR_BLACK;
    if (c.r > 192 && c.g > 192 && c.b > 192) return VGA_COLOR_WHITE;
    if (c.r > 128 && c.g < 64 && c.b < 64)  return VGA_COLOR_RED;
    if (c.r < 64 && c.g > 128 && c.b < 64)  return VGA_COLOR_GREEN;
    if (c.r < 64 && c.g < 64 && c.b > 128)  return VGA_COLOR_BLUE;
    if (c.r > 128 && c.g > 128 && c.b < 64) return VGA_COLOR_LIGHT_BROWN;
    if (c.r < 64 && c.g > 128 && c.b > 128) return VGA_COLOR_CYAN;
    if (c.r > 128 && c.g < 64 && c.b > 128) return VGA_COLOR_MAGENTA;
    if (c.r > 128 && c.g > 64 && c.b < 64)  return VGA_COLOR_BROWN;
    if (c.r < 128 && c.g < 128 && c.b < 128) return VGA_COLOR_DARK_GREY;
    return VGA_COLOR_LIGHT_GREY;
}

/* -------------------------------------------------------------------------
 * String length (no libc in kernel)
 * ------------------------------------------------------------------------- */
static int dps_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

/* -------------------------------------------------------------------------
 * Compositor
 * ------------------------------------------------------------------------- */

void compositor_draw_desktop(void)
{
    /*
     * Fill the entire VGA text grid with the desktop pattern:
     *   - Row 0: menu bar (dark grey background, white text)
     *   - Rows 1-23: desktop surface (dark blue, space chars)
     *   - Row 24: status bar (dark grey, dim text)
     */

    /* Menu bar */
    for (int col = 0; col < SCREEN_COLS; col++) {
        vga_write_at((uint8_t)col, 0, ' ',
                     VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    }
    vga_write_at(1, 0, 'U', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    vga_write_at(2, 0, 'N', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    vga_write_at(3, 0, 'H', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    vga_write_at(4, 0, 'O', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    vga_write_at(5, 0, 'X', VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
    vga_write_at(7, 0, 'F', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(8, 0, 'i', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(9, 0, 'l', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(10, 0, 'e', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(12, 0, 'E', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(13, 0, 'd', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(14, 0, 'i', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(15, 0, 't', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(17, 0, 'W', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(18, 0, 'i', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(19, 0, 'n', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(20, 0, 'd', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(21, 0, 'o', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    vga_write_at(22, 0, 'w', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    /* Desktop surface: rows 1-23 */
    for (int row = 1; row < SCREEN_ROWS - 1; row++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            vga_write_at((uint8_t)col, (uint8_t)row, ' ',
                         VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
        }
    }

    /* Status bar: row 24 */
    for (int col = 0; col < SCREEN_COLS; col++) {
        vga_write_at((uint8_t)col, SCREEN_ROWS - 1, ' ',
                     VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    }
    /* Show "UNHOX v1.0" in the status bar */
    const char *ver = "UNHOX v1.0  Milestone: NeXT-heritage desktop";
    for (int i = 0; ver[i] && i < SCREEN_COLS - 1; i++) {
        vga_write_at((uint8_t)(i + 1), SCREEN_ROWS - 1, ver[i],
                     VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);
    }
}

void compositor_draw_window_frame(const dps_window_t *win)
{
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE) ||
        !(win->flags & DPS_WIN_FLAG_VISIBLE))
        return;

    /* Convert pixel origin to cell coordinates */
    int col0  = (int)PX_TO_COL(win->x);
    int row0  = (int)PX_TO_ROW(win->y);
    int cols  = (int)PX_TO_COL(win->width);
    int rows  = (int)PX_TO_ROW(win->height);

    /* Clamp to VGA screen bounds */
    if (col0 < 0) col0 = 0;
    if (row0 < 1) row0 = 1;  /* row 0 is the menu bar */
    if (col0 + cols > SCREEN_COLS) cols = SCREEN_COLS - col0;
    if (row0 + rows > SCREEN_ROWS - 1)
        rows = SCREEN_ROWS - 1 - row0;
    if (cols <= 0 || rows <= 0) return;

    vga_color_t title_bg = (win->flags & DPS_WIN_FLAG_FOCUSED)
        ? VGA_COLOR_BLUE : VGA_COLOR_DARK_GREY;

    /* Title bar (first row of window) */
    for (int c = col0; c < col0 + cols; c++) {
        vga_write_at((uint8_t)c, (uint8_t)row0, ' ',
                     VGA_COLOR_WHITE, title_bg);
    }

    /* Write title text (centred, truncated) */
    int title_len = dps_strlen(win->title);
    int title_col = col0 + (cols - title_len) / 2;
    if (title_col < col0) title_col = col0;
    for (int i = 0; win->title[i] && (title_col + i) < col0 + cols; i++) {
        vga_write_at((uint8_t)(title_col + i), (uint8_t)row0,
                     win->title[i], VGA_COLOR_WHITE, title_bg);
    }

    /* Window content area: rows row0+1 .. row0+rows-1 */
    for (int r = row0 + 1; r < row0 + rows; r++) {
        if (r >= SCREEN_ROWS - 1) break;
        for (int c = col0; c < col0 + cols; c++) {
            vga_write_at((uint8_t)c, (uint8_t)r, ' ',
                         VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        }
    }

    /* Draw a simple border: left/right edges of content rows */
    for (int r = row0 + 1; r < row0 + rows && r < SCREEN_ROWS - 1; r++) {
        vga_write_at((uint8_t)col0, (uint8_t)r, '|',
                     VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY);
        if (col0 + cols - 1 < SCREEN_COLS)
            vga_write_at((uint8_t)(col0 + cols - 1), (uint8_t)r, '|',
                         VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY);
    }
}

void compositor_redraw_all(void)
{
    compositor_draw_desktop();
    for (int i = 0; i < DPS_MAX_WINDOWS; i++) {
        if (dps_windows[i].flags & DPS_WIN_FLAG_ACTIVE)
            compositor_draw_window_frame(&dps_windows[i]);
    }
}

void compositor_init(void)
{
    vga_init();
    for (int i = 0; i < DPS_MAX_WINDOWS; i++) {
        dps_windows[i].flags = 0;
        dps_windows[i].wid   = 0;
    }
    dps_window_count = 0;
    compositor_draw_desktop();
    serial_putstr("[display] compositor initialised (VGA text mode 80x25)\r\n");
}

/* -------------------------------------------------------------------------
 * Window management
 * ------------------------------------------------------------------------- */

int32_t dps_window_create(int32_t x, int32_t y,
                           uint32_t width, uint32_t height,
                           const char *title)
{
    if (dps_window_count >= DPS_MAX_WINDOWS)
        return -DPS_LIMIT_REACHED;

    for (int i = 0; i < DPS_MAX_WINDOWS; i++) {
        if (!(dps_windows[i].flags & DPS_WIN_FLAG_ACTIVE)) {
            dps_windows[i].wid    = i + 1;
            dps_windows[i].x      = x;
            dps_windows[i].y      = y;
            dps_windows[i].width  = width;
            dps_windows[i].height = height;
            dps_windows[i].flags  = DPS_WIN_FLAG_ACTIVE | DPS_WIN_FLAG_VISIBLE;

            /* Copy title with bounds check */
            int tlen = dps_strlen(title);
            if (tlen >= DPS_TITLE_MAX) tlen = DPS_TITLE_MAX - 1;
            for (int k = 0; k < tlen; k++)
                dps_windows[i].title[k] = title[k];
            dps_windows[i].title[tlen] = '\0';

            dps_window_count++;
            compositor_draw_window_frame(&dps_windows[i]);
            return dps_windows[i].wid;
        }
    }
    return -DPS_LIMIT_REACHED;
}

int32_t dps_window_destroy(int32_t wid)
{
    if (wid <= 0 || wid > DPS_MAX_WINDOWS)
        return -DPS_INVALID_WID;
    dps_window_t *win = &dps_windows[wid - 1];
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE))
        return -DPS_INVALID_WID;

    win->flags = 0;
    win->wid   = 0;
    dps_window_count--;
    compositor_redraw_all();
    return DPS_SUCCESS;
}

int32_t dps_window_move(int32_t wid, int32_t x, int32_t y)
{
    if (wid <= 0 || wid > DPS_MAX_WINDOWS)
        return -DPS_INVALID_WID;
    dps_window_t *win = &dps_windows[wid - 1];
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE))
        return -DPS_INVALID_WID;

    win->x = x;
    win->y = y;
    compositor_redraw_all();
    return DPS_SUCCESS;
}

int32_t dps_window_resize(int32_t wid, uint32_t width, uint32_t height)
{
    if (wid <= 0 || wid > DPS_MAX_WINDOWS)
        return -DPS_INVALID_WID;
    dps_window_t *win = &dps_windows[wid - 1];
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE))
        return -DPS_INVALID_WID;

    win->width  = width;
    win->height = height;
    compositor_redraw_all();
    return DPS_SUCCESS;
}

int32_t dps_draw_rect(int32_t wid, int32_t x, int32_t y,
                      uint32_t w, uint32_t h, dps_color_t color)
{
    if (wid <= 0 || wid > DPS_MAX_WINDOWS)
        return -DPS_INVALID_WID;
    dps_window_t *win = &dps_windows[wid - 1];
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE))
        return -DPS_INVALID_WID;

    /* Convert window-relative pixel coords to screen cell coords */
    int col0 = (int)PX_TO_COL(win->x + x);
    int row0 = (int)PX_TO_ROW(win->y + y) + 1;  /* +1: skip title row */
    int cols = (int)PX_TO_COL(w); if (cols < 1) cols = 1;
    int rows = (int)PX_TO_ROW(h); if (rows < 1) rows = 1;

    vga_color_t vga_col = dps_to_vga_color(color);
    for (int r = row0; r < row0 + rows && r < SCREEN_ROWS - 1; r++) {
        for (int c = col0; c < col0 + cols && c < SCREEN_COLS; c++) {
            if (r > 0)
                vga_write_at((uint8_t)c, (uint8_t)r, ' ',
                             vga_col, vga_col);
        }
    }
    return DPS_SUCCESS;
}

int32_t dps_draw_text(int32_t wid, int32_t x, int32_t y,
                      dps_color_t fg, dps_color_t bg,
                      const char *text)
{
    if (wid <= 0 || wid > DPS_MAX_WINDOWS)
        return -DPS_INVALID_WID;
    dps_window_t *win = &dps_windows[wid - 1];
    if (!(win->flags & DPS_WIN_FLAG_ACTIVE))
        return -DPS_INVALID_WID;

    int col = (int)PX_TO_COL(win->x + x);
    int row = (int)PX_TO_ROW(win->y + y) + 1;  /* +1: skip title row */

    if (row < 1 || row >= SCREEN_ROWS - 1) return DPS_SUCCESS;

    vga_color_t vfg = dps_to_vga_color(fg);
    vga_color_t vbg = dps_to_vga_color(bg);
    for (int i = 0; text[i] && col + i < SCREEN_COLS; i++) {
        vga_write_at((uint8_t)(col + i), (uint8_t)row,
                     text[i], vfg, vbg);
    }
    return DPS_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Server main loop
 * ------------------------------------------------------------------------- */

void display_server_main(void)
{
    serial_putstr("[display] display server starting\r\n");
    compositor_init();

    /* Allocate the display server's receive port */
    display_port = ipc_port_alloc(kernel_task_ptr());
    if (!display_port) {
        serial_putstr("[display] FATAL: failed to allocate display port\r\n");
        for (;;) __asm__ volatile ("hlt");
    }

    /* Register with bootstrap */
    if (bootstrap_port) {
        bootstrap_register_msg_t reg;
        reg.hdr.msgh_id = BOOTSTRAP_MSG_REGISTER;
        reg.hdr.msgh_size = sizeof(reg);
        reg.hdr.msgh_remote_port = 0;
        reg.hdr.msgh_local_port  = 0;
        reg.hdr.msgh_voucher_port = 0;
        reg.hdr.msgh_bits = 0;
        for (int i = 0; i < BOOTSTRAP_NAME_MAX - 1 && "com.unhox.display"[i]; i++)
            reg.name[i] = "com.unhox.display"[i];
        reg.name[BOOTSTRAP_NAME_MAX - 1] = '\0';
        reg.service_port = (uint64_t)(uintptr_t)display_port;
        ipc_mqueue_send(bootstrap_port->ip_messages,
                        (mach_msg_header_t *)&reg, sizeof(reg));
        serial_putstr("[display] registered as com.unhox.display\r\n");
    }

    serial_putstr("[display] display server ready\r\n");

    uint8_t buf[512];
    for (;;) {
        mach_msg_size_t msg_size = 0;
        mach_msg_return_t mr = ipc_mqueue_receive(
            display_port->ip_messages,
            buf, sizeof(buf), &msg_size, 1 /* blocking */);

        if (mr != MACH_MSG_SUCCESS)
            continue;

        mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

        switch (hdr->msgh_id) {

        case DPS_MSG_WINDOW_CREATE: {
            if (msg_size < sizeof(dps_window_create_msg_t)) break;
            dps_window_create_msg_t *req = (dps_window_create_msg_t *)buf;
            req->title[DPS_TITLE_MAX - 1] = '\0';
            int32_t wid = dps_window_create(req->x, req->y,
                                            req->width, req->height,
                                            req->title);
            serial_putstr("[display] WINDOW_CREATE: ");
            serial_putstr(req->title);
            serial_putstr(wid > 0 ? " OK\r\n" : " FAILED\r\n");

            /* Send reply if reply port was supplied */
            if (req->reply_port) {
                struct ipc_port *rp = (struct ipc_port *)req->reply_port;
                dps_reply_msg_t reply;
                reply.hdr.msgh_id = DPS_MSG_REPLY;
                reply.hdr.msgh_size = sizeof(reply);
                reply.hdr.msgh_remote_port  = 0;
                reply.hdr.msgh_local_port   = 0;
                reply.hdr.msgh_voucher_port = 0;
                reply.hdr.msgh_bits = 0;
                reply.retcode = (wid > 0) ? DPS_SUCCESS : -wid;
                reply.result  = (wid > 0) ? wid : 0;
                ipc_mqueue_send(rp->ip_messages,
                                (mach_msg_header_t *)&reply, sizeof(reply));
            }
            break;
        }

        case DPS_MSG_WINDOW_DESTROY: {
            if (msg_size < sizeof(dps_window_destroy_msg_t)) break;
            dps_window_destroy_msg_t *req = (dps_window_destroy_msg_t *)buf;
            dps_window_destroy(req->wid);
            serial_putstr("[display] WINDOW_DESTROY\r\n");
            break;
        }

        case DPS_MSG_WINDOW_MOVE: {
            if (msg_size < sizeof(dps_window_move_msg_t)) break;
            dps_window_move_msg_t *req = (dps_window_move_msg_t *)buf;
            dps_window_move(req->wid, req->x, req->y);
            break;
        }

        case DPS_MSG_WINDOW_RESIZE: {
            if (msg_size < sizeof(dps_window_resize_msg_t)) break;
            dps_window_resize_msg_t *req = (dps_window_resize_msg_t *)buf;
            dps_window_resize(req->wid, req->width, req->height);
            break;
        }

        case DPS_MSG_DRAW_RECT: {
            if (msg_size < sizeof(dps_draw_rect_msg_t)) break;
            dps_draw_rect_msg_t *req = (dps_draw_rect_msg_t *)buf;
            dps_draw_rect(req->wid, req->x, req->y,
                          req->width, req->height, req->color);
            break;
        }

        case DPS_MSG_DRAW_TEXT: {
            if (msg_size < sizeof(dps_draw_text_msg_t)) break;
            dps_draw_text_msg_t *req = (dps_draw_text_msg_t *)buf;
            req->text[DPS_TEXT_MAX - 1] = '\0';
            dps_draw_text(req->wid, req->x, req->y,
                          req->fg, req->bg, req->text);
            break;
        }

        case DPS_MSG_FLUSH:
            /* v1: drawing is immediate; flush is a no-op */
            break;

        default:
            serial_putstr("[display] unknown message id\r\n");
            break;
        }
    }
}
