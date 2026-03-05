/*
 * frameworks/DisplayServer/display_server.c — UNHOX Display Server
 *
 * DPS-inspired compositing server over VGA text mode (80x25, 16 colors).
 * Runs as a kernel thread. Receives commands via Mach IPC on a port
 * registered as "com.unhox.display" with the bootstrap server.
 *
 * Features:
 *   - Up to DPS_MAX_WINDOWS windows with title bars and content areas
 *   - Z-ordered window stack (higher z_order = on top)
 *   - PS/2 mouse cursor with click-to-focus and title-bar drag
 *   - Non-blocking IPC + mouse polling loop
 */

#include "display_server.h"
#include "device/vga_text.h"
#include "device/mouse.h"
#include "kern/klib.h"
#include "kern/task.h"
#include "kern/sched.h"
#include "ipc/ipc_port.h"
#include "ipc/ipc_mqueue.h"
#include "bootstrap/bootstrap.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t v);

/* -------------------------------------------------------------------------
 * Window table + z-order
 * ------------------------------------------------------------------------- */

static dps_window_t windows[DPS_MAX_WINDOWS];
static int z_order[DPS_MAX_WINDOWS];    /* window id at each z level */
static int z_count;                      /* number of active z levels */
static int focused_window = -1;          /* currently focused window id */
static struct ipc_port *display_port;

/* -------------------------------------------------------------------------
 * Mouse cursor state
 * ------------------------------------------------------------------------- */

static int16_t cursor_x = 40;   /* character-cell position */
static int16_t cursor_y = 12;

/* Drag state */
static int dragging = -1;       /* window id being dragged, or -1 */
static int16_t drag_off_x;      /* offset from window origin to cursor */
static int16_t drag_off_y;
static uint8_t prev_buttons;

/* -------------------------------------------------------------------------
 * Z-order management
 * ------------------------------------------------------------------------- */

static void z_add(int id)
{
    z_order[z_count++] = id;
}

static void z_remove(int id)
{
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos < 0) return;
    for (int i = pos; i < z_count - 1; i++)
        z_order[i] = z_order[i + 1];
    z_count--;
}

static void z_raise(int id)
{
    z_remove(id);
    z_add(id);
    focused_window = id;
}

/* -------------------------------------------------------------------------
 * Compositor — draws to VGA text buffer
 * ------------------------------------------------------------------------- */

static const char *menubar_text = " UNHOX   File  Edit  Windows  Help";

/* -------------------------------------------------------------------------
 * Menu bar state
 * ------------------------------------------------------------------------- */

/* Menu item column ranges on the menu bar (inclusive start, exclusive end) */
#define MENU_UNHOX_X0   1
#define MENU_UNHOX_X1   6
#define MENU_FILE_X0    9
#define MENU_FILE_X1    13
#define MENU_EDIT_X0    15
#define MENU_EDIT_X1    19
#define MENU_WIN_X0     21
#define MENU_WIN_X1     28
#define MENU_HELP_X0    30
#define MENU_HELP_X1    34

/* Which menu is open: 0=none, 1=UNHOX, 2=File, 3=Edit, 4=Windows, 5=Help */
static int menu_open;
static int about_window_id = -1;  /* track the About window */
static int bootlog_window_id = -1; /* track the Boot Log window */

/* -------------------------------------------------------------------------
 * Boot log capture ring buffer
 * ------------------------------------------------------------------------- */

#define BOOTLOG_LINES   64
#define BOOTLOG_COLS    76
static char bootlog[BOOTLOG_LINES][BOOTLOG_COLS];
static int  bootlog_count;    /* number of lines stored */
static int  bootlog_cur_col;  /* current column in current line */

void bootlog_putchar(char c)
{
    if (bootlog_count == 0)
        bootlog_count = 1;

    int line = bootlog_count - 1;
    if (line >= BOOTLOG_LINES)
        return;  /* buffer full */

    if (c == '\n' || c == '\r') {
        if (c == '\n') {
            /* Advance to next line */
            bootlog_count++;
            bootlog_cur_col = 0;
        }
        return;
    }

    if (bootlog_cur_col < BOOTLOG_COLS - 1) {
        bootlog[line][bootlog_cur_col++] = c;
        bootlog[line][bootlog_cur_col] = '\0';
    }
}

void bootlog_putstr(const char *s)
{
    while (*s)
        bootlog_putchar(*s++);
}

void compositor_init(void)
{
    for (int i = 0; i < DPS_MAX_WINDOWS; i++)
        windows[i].active = 0;
    z_count = 0;
    focused_window = -1;

    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    for (uint8_t x = 0; x < VGA_WIDTH; x++)
        vga_write_at(x, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t mx = 0;
    for (const char *p = menubar_text; *p && mx < VGA_WIDTH; p++, mx++)
        vga_write_at(mx, 0, *p, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void compositor_draw_desktop(void)
{
    for (uint8_t y = 1; y < VGA_HEIGHT; y++)
        for (uint8_t x = 0; x < VGA_WIDTH; x++)
            vga_write_at(x, y, ' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_DARK_GREY);

    for (uint8_t x = 0; x < VGA_WIDTH; x++)
        vga_write_at(x, 0, ' ', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t mx = 0;
    for (const char *p = menubar_text; *p && mx < VGA_WIDTH; p++, mx++)
        vga_write_at(mx, 0, *p, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void compositor_draw_window(int id)
{
    dps_window_t *w = &windows[id];
    if (!w->active)
        return;

    uint8_t x0 = w->x;
    uint8_t y0 = w->y;
    uint8_t x1 = x0 + w->w - 1;
    uint8_t y1 = y0 + w->h - 1;

    if (x1 >= VGA_WIDTH)  x1 = VGA_WIDTH - 1;
    if (y1 >= VGA_HEIGHT) y1 = VGA_HEIGHT - 1;

    /* Title bar: focused = white on blue, unfocused = light grey on dark grey */
    vga_color_t tb_fg, tb_bg;
    if (id == focused_window) {
        tb_fg = VGA_COLOR_WHITE;
        tb_bg = VGA_COLOR_BLUE;
    } else {
        tb_fg = VGA_COLOR_LIGHT_GREY;
        tb_bg = VGA_COLOR_DARK_GREY;
    }

    for (uint8_t x = x0; x <= x1; x++)
        vga_write_at(x, y0, ' ', tb_fg, tb_bg);

    /* Title text centered */
    int title_len = 0;
    while (w->title[title_len] && title_len < DPS_TITLE_MAX)
        title_len++;
    int title_start = x0 + (w->w - title_len) / 2;
    if (title_start < x0) title_start = x0;
    for (int i = 0; i < title_len && (title_start + i) <= x1; i++)
        vga_write_at((uint8_t)(title_start + i), y0, w->title[i], tb_fg, tb_bg);

    /* Close button */
    if (x0 <= x1)
        vga_write_at(x0, y0, 'x', VGA_COLOR_LIGHT_RED, tb_bg);

    /* Content area */
    uint8_t content_h = (y1 > y0) ? (y1 - y0) : 0;
    uint8_t content_w = (x1 >= x0) ? (x1 - x0 + 1) : 0;

    for (uint8_t cy = 0; cy < content_h; cy++) {
        uint8_t sy = y0 + 1 + cy;
        if (sy > y1) break;
        for (uint8_t cx = 0; cx < content_w; cx++) {
            uint8_t sx = x0 + cx;
            if (sx > x1) break;
            char ch = w->content[cy][cx];
            uint8_t cfg = w->cfgmap[cy][cx];
            uint8_t cbg = w->cbgmap[cy][cx];
            if (ch == 0) ch = ' ';
            if (cfg == 0 && cbg == 0) {
                cfg = w->fg ? w->fg : VGA_COLOR_BLACK;
                cbg = w->bg ? w->bg : VGA_COLOR_WHITE;
            }
            vga_write_at(sx, sy, ch, (vga_color_t)cfg, (vga_color_t)cbg);
        }
    }
}

static void compositor_draw_cursor(void)
{
    /* Draw mouse cursor as a reverse-video block character */
    uint8_t cx = (uint8_t)cursor_x;
    uint8_t cy = (uint8_t)cursor_y;
    if (cx < VGA_WIDTH && cy < VGA_HEIGHT)
        vga_write_at(cx, cy, '\xDB', VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* Draw a dropdown menu overlay */
static void compositor_draw_menu(void)
{
    if (!menu_open)
        return;

    uint8_t mx, my, mw, mh;
    const char *items[6];
    int item_count = 0;

    switch (menu_open) {
    case 1: /* UNHOX menu */
        mx = MENU_UNHOX_X0;
        my = 1;
        mw = 22;
        mh = 4;
        items[0] = " About UNHOX       ";
        items[1] = "                   ";
        items[2] = " Preferences...    ";
        item_count = 3;
        break;
    case 2: /* File menu */
        mx = MENU_FILE_X0;
        my = 1;
        mw = 18;
        mh = 4;
        items[0] = " New Window     ";
        items[1] = " Close Window   ";
        items[2] = "                ";
        item_count = 3;
        break;
    case 5: /* Help menu */
        mx = MENU_HELP_X0;
        my = 1;
        mw = 22;
        mh = 3;
        items[0] = " UNHOX Help        ";
        items[1] = " Mach IPC Guide    ";
        item_count = 2;
        break;
    case 4: /* Windows menu */
        mx = MENU_WIN_X0;
        my = 1;
        mw = 20;
        mh = 3;
        items[0] = " Boot Log          ";
        items[1] = " Tile Windows      ";
        item_count = 2;
        break;
    default: /* Edit — placeholder */
        mx = MENU_EDIT_X0;
        my = 1;
        mw = 16;
        mh = 2;
        items[0] = " (no items)   ";
        item_count = 1;
        break;
    }

    /* Draw menu background */
    for (uint8_t y = my; y < my + mh && y < VGA_HEIGHT; y++)
        for (uint8_t x = mx; x < mx + mw && x < VGA_WIDTH; x++)
            vga_write_at(x, y, ' ', VGA_COLOR_BLACK, VGA_COLOR_WHITE);

    /* Draw menu items */
    for (int i = 0; i < item_count; i++) {
        uint8_t iy = my + i;
        if (iy >= VGA_HEIGHT) break;

        /* Highlight item under cursor */
        int highlighted = (cursor_y == iy && cursor_x >= mx && cursor_x < mx + mw);
        vga_color_t ifg = highlighted ? VGA_COLOR_WHITE : VGA_COLOR_BLACK;
        vga_color_t ibg = highlighted ? VGA_COLOR_BLUE  : VGA_COLOR_WHITE;

        for (uint8_t x = mx; x < mx + mw && x < VGA_WIDTH; x++)
            vga_write_at(x, iy, ' ', ifg, ibg);

        const char *text = items[i];
        for (int j = 0; text[j] && (mx + j) < VGA_WIDTH; j++)
            vga_write_at((uint8_t)(mx + j), iy, text[j], ifg, ibg);
    }

    /* Highlight the active menu bar item */
    uint8_t hlx0, hlx1;
    switch (menu_open) {
    case 1: hlx0 = MENU_UNHOX_X0; hlx1 = MENU_UNHOX_X1; break;
    case 2: hlx0 = MENU_FILE_X0;  hlx1 = MENU_FILE_X1;  break;
    case 3: hlx0 = MENU_EDIT_X0;  hlx1 = MENU_EDIT_X1;  break;
    case 4: hlx0 = MENU_WIN_X0;   hlx1 = MENU_WIN_X1;   break;
    case 5: hlx0 = MENU_HELP_X0;  hlx1 = MENU_HELP_X1;  break;
    default: hlx0 = 0; hlx1 = 0; break;
    }
    for (uint8_t x = hlx0; x < hlx1; x++) {
        char c = (x < 80 && menubar_text[x]) ? menubar_text[x] : ' ';
        vga_write_at(x, 0, c, VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    }
}

void compositor_redraw_all(void)
{
    compositor_draw_desktop();

    /* Draw windows in z-order (back to front) */
    for (int i = 0; i < z_count; i++) {
        int id = z_order[i];
        if (id >= 0 && id < DPS_MAX_WINDOWS && windows[id].active)
            compositor_draw_window(id);
    }

    /* Draw dropdown menu on top of windows */
    compositor_draw_menu();

    /* Cursor always on top */
    compositor_draw_cursor();
}

/* -------------------------------------------------------------------------
 * Window management
 * ------------------------------------------------------------------------- */

static int window_create(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                         const char *title, uint8_t fg, uint8_t bg)
{
    int id = -1;
    for (int i = 0; i < DPS_MAX_WINDOWS; i++) {
        if (!windows[i].active) { id = i; break; }
    }
    if (id < 0)
        return -1;

    dps_window_t *win = &windows[id];
    kmemset(win, 0, sizeof(dps_window_t));
    win->active = 1;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->fg = fg ? fg : VGA_COLOR_BLACK;
    win->bg = bg ? bg : VGA_COLOR_WHITE;
    kstrncpy(win->title, title, DPS_TITLE_MAX - 1);
    win->title[DPS_TITLE_MAX - 1] = '\0';

    for (int cy = 0; cy < 23; cy++) {
        for (int cx = 0; cx < 78; cx++) {
            win->content[cy][cx] = ' ';
            win->cfgmap[cy][cx] = win->fg;
            win->cbgmap[cy][cx] = win->bg;
        }
    }

    z_add(id);
    focused_window = id;

    return id;
}

static void window_draw_text(int id, uint8_t x, uint8_t y,
                             const char *text, uint8_t fg, uint8_t bg)
{
    if (id < 0 || id >= DPS_MAX_WINDOWS || !windows[id].active)
        return;

    dps_window_t *win = &windows[id];
    uint8_t content_w = (win->w > 0) ? win->w : 1;
    uint8_t content_h = (win->h > 1) ? (win->h - 1) : 1;

    if (y >= content_h) return;

    for (int i = 0; text[i] && (x + i) < content_w && (x + i) < 78; i++) {
        win->content[y][x + i] = text[i];
        win->cfgmap[y][x + i] = fg;
        win->cbgmap[y][x + i] = bg;
    }
}

static void window_draw_rect(int id, uint8_t rx, uint8_t ry,
                             uint8_t rw, uint8_t rh,
                             uint8_t fg, uint8_t bg)
{
    if (id < 0 || id >= DPS_MAX_WINDOWS || !windows[id].active)
        return;

    dps_window_t *win = &windows[id];
    for (uint8_t cy = ry; cy < ry + rh && cy < 23; cy++) {
        for (uint8_t cx = rx; cx < rx + rw && cx < 78; cx++) {
            win->content[cy][cx] = ' ';
            win->cfgmap[cy][cx] = fg;
            win->cbgmap[cy][cx] = bg;
        }
    }
}

/* -------------------------------------------------------------------------
 * Boot Log window
 * ------------------------------------------------------------------------- */

static void open_bootlog_window(void)
{
    if (bootlog_window_id >= 0 && windows[bootlog_window_id].active) {
        z_raise(bootlog_window_id);
        return;
    }

    /* Create a large window for the boot log */
    bootlog_window_id = window_create(2, 2, 76, 22,
        "Boot Log", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    if (bootlog_window_id < 0)
        return;

    /* Fill with boot log lines (show last lines that fit the window) */
    int visible = 20;  /* content rows (22 total - 1 title - 1 padding) */
    int start = 0;
    if (bootlog_count > visible)
        start = bootlog_count - visible;

    for (int i = start; i < bootlog_count && (i - start) < visible; i++) {
        window_draw_text(bootlog_window_id, 0, (uint8_t)(i - start),
                         bootlog[i], VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    }
}

/* -------------------------------------------------------------------------
 * Hit testing + mouse interaction
 * ------------------------------------------------------------------------- */

/* Find topmost window at screen position (sx, sy). Returns id or -1. */
static int hit_test(int16_t sx, int16_t sy)
{
    /* Walk z-order back to front, return last (topmost) hit */
    int hit = -1;
    for (int i = 0; i < z_count; i++) {
        int id = z_order[i];
        if (id < 0 || id >= DPS_MAX_WINDOWS || !windows[id].active)
            continue;
        dps_window_t *w = &windows[id];
        if (sx >= w->x && sx < w->x + w->w &&
            sy >= w->y && sy < w->y + w->h) {
            hit = id;
        }
    }
    return hit;
}

/* Check if (sx, sy) is on the title bar of window id */
static int is_title_bar(int id, int16_t sx, int16_t sy)
{
    dps_window_t *w = &windows[id];
    return (sy == w->y && sx >= w->x && sx < w->x + w->w);
}

static void handle_mouse(void)
{
    mouse_event_t ev;
    if (!mouse_read_event(&ev))
        return;

    /* Update cursor position (PS/2 Y is inverted: up = positive) */
    cursor_x += ev.dx;
    cursor_y -= ev.dy;

    /* Clamp to screen */
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= VGA_WIDTH) cursor_x = VGA_WIDTH - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;

    uint8_t btn = ev.buttons;
    uint8_t left_pressed  = (btn & MOUSE_BTN_LEFT) && !(prev_buttons & MOUSE_BTN_LEFT);
    uint8_t left_released = !(btn & MOUSE_BTN_LEFT) && (prev_buttons & MOUSE_BTN_LEFT);

    /* Handle drag in progress */
    if (dragging >= 0) {
        if (btn & MOUSE_BTN_LEFT) {
            /* Move window */
            int16_t nx = cursor_x - drag_off_x;
            int16_t ny = cursor_y - drag_off_y;
            if (nx < 0) nx = 0;
            if (ny < 1) ny = 1;  /* don't overlap menu bar */
            if (nx + windows[dragging].w > VGA_WIDTH)
                nx = VGA_WIDTH - windows[dragging].w;
            if (ny + windows[dragging].h > VGA_HEIGHT)
                ny = VGA_HEIGHT - windows[dragging].h;
            windows[dragging].x = (uint8_t)nx;
            windows[dragging].y = (uint8_t)ny;
        }
        if (left_released) {
            dragging = -1;
        }
        prev_buttons = btn;
        compositor_redraw_all();
        return;
    }

    /* Left click */
    if (left_pressed) {
        /* --- Menu bar clicks (row 0) --- */
        if (cursor_y == 0) {
            int clicked_menu = 0;
            if (cursor_x >= MENU_UNHOX_X0 && cursor_x < MENU_UNHOX_X1)
                clicked_menu = 1;
            else if (cursor_x >= MENU_FILE_X0 && cursor_x < MENU_FILE_X1)
                clicked_menu = 2;
            else if (cursor_x >= MENU_EDIT_X0 && cursor_x < MENU_EDIT_X1)
                clicked_menu = 3;
            else if (cursor_x >= MENU_WIN_X0 && cursor_x < MENU_WIN_X1)
                clicked_menu = 4;
            else if (cursor_x >= MENU_HELP_X0 && cursor_x < MENU_HELP_X1)
                clicked_menu = 5;

            if (clicked_menu) {
                /* Toggle: clicking the same menu closes it */
                menu_open = (menu_open == clicked_menu) ? 0 : clicked_menu;
            } else {
                menu_open = 0;
            }
            prev_buttons = btn;
            compositor_redraw_all();
            return;
        }

        /* --- Menu item selection (if a dropdown is open) --- */
        if (menu_open) {
            uint8_t mx, mw, my;
            int item_count = 0;
            switch (menu_open) {
            case 1: mx = MENU_UNHOX_X0; mw = 22; my = 1; item_count = 3; break;
            case 2: mx = MENU_FILE_X0;  mw = 18; my = 1; item_count = 3; break;
            case 4: mx = MENU_WIN_X0;   mw = 20; my = 1; item_count = 2; break;
            case 5: mx = MENU_HELP_X0;  mw = 22; my = 1; item_count = 2; break;
            default: mx = MENU_EDIT_X0; mw = 16; my = 1; item_count = 1; break;
            }

            int in_dropdown = (cursor_x >= mx && cursor_x < mx + mw &&
                               cursor_y >= my && cursor_y < my + item_count);
            if (in_dropdown) {
                int item_idx = cursor_y - my;

                /* Handle menu actions */
                if (menu_open == 1 && item_idx == 0) {
                    /* "About UNHOX" */
                    if (about_window_id >= 0 && windows[about_window_id].active) {
                        z_raise(about_window_id);
                    } else {
                        about_window_id = window_create(18, 7, 44, 10,
                            "About UNHOX", VGA_COLOR_BLACK, VGA_COLOR_WHITE);
                        if (about_window_id >= 0) {
                            window_draw_text(about_window_id, 1, 1,
                                "       U N H O X", VGA_COLOR_BLUE, VGA_COLOR_WHITE);
                            window_draw_text(about_window_id, 1, 2,
                                "  U is Not Hurd Or X", VGA_COLOR_BLACK, VGA_COLOR_WHITE);
                            window_draw_text(about_window_id, 1, 4,
                                "  Mach microkernel OS", VGA_COLOR_BLACK, VGA_COLOR_WHITE);
                            window_draw_text(about_window_id, 1, 5,
                                "  Version 1.0 (Phase 5)", VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
                            window_draw_text(about_window_id, 1, 7,
                                "  (c) 2026 UNHOX Project", VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
                        }
                    }
                }
                /* Windows > Boot Log */
                if (menu_open == 4 && item_idx == 0) {
                    open_bootlog_window();
                }
            }

            menu_open = 0;
            prev_buttons = btn;
            compositor_redraw_all();
            return;
        }

        /* --- Window interaction --- */
        int hit = hit_test(cursor_x, cursor_y);
        if (hit >= 0) {
            /* Close button? (first cell of title bar) */
            if (is_title_bar(hit, cursor_x, cursor_y) &&
                cursor_x == windows[hit].x) {
                windows[hit].active = 0;
                z_remove(hit);
                if (focused_window == hit)
                    focused_window = (z_count > 0) ? z_order[z_count - 1] : -1;
                if (hit == about_window_id)
                    about_window_id = -1;
                if (hit == bootlog_window_id)
                    bootlog_window_id = -1;
            } else {
                /* Raise and focus */
                z_raise(hit);

                /* Start drag if on title bar */
                if (is_title_bar(hit, cursor_x, cursor_y)) {
                    dragging = hit;
                    drag_off_x = cursor_x - windows[hit].x;
                    drag_off_y = cursor_y - windows[hit].y;
                }
            }
        } else {
            /* Click on desktop — unfocus and close menu */
            focused_window = -1;
            menu_open = 0;
        }
    } else if (menu_open && !left_pressed) {
        /* Hovering over menu bar while menu is open — switch menus */
        if (cursor_y == 0) {
            int hover_menu = 0;
            if (cursor_x >= MENU_UNHOX_X0 && cursor_x < MENU_UNHOX_X1)
                hover_menu = 1;
            else if (cursor_x >= MENU_FILE_X0 && cursor_x < MENU_FILE_X1)
                hover_menu = 2;
            else if (cursor_x >= MENU_EDIT_X0 && cursor_x < MENU_EDIT_X1)
                hover_menu = 3;
            else if (cursor_x >= MENU_WIN_X0 && cursor_x < MENU_WIN_X1)
                hover_menu = 4;
            else if (cursor_x >= MENU_HELP_X0 && cursor_x < MENU_HELP_X1)
                hover_menu = 5;
            if (hover_menu && hover_menu != menu_open)
                menu_open = hover_menu;
        }
    }

    prev_buttons = btn;
    compositor_redraw_all();
}

/* -------------------------------------------------------------------------
 * IPC message handlers
 * ------------------------------------------------------------------------- */

static void handle_window_create(const uint8_t *buf)
{
    const dps_window_create_msg_t *msg = (const dps_window_create_msg_t *)buf;
    int id = window_create(msg->x, msg->y, msg->w, msg->h,
                           msg->title, msg->fg, msg->bg);

    if (msg->reply_port) {
        struct ipc_port *rp = (struct ipc_port *)(uintptr_t)msg->reply_port;
        dps_reply_msg_t reply;
        kmemset(&reply, 0, sizeof(reply));
        reply.hdr.msgh_size = sizeof(reply);
        reply.hdr.msgh_id = DPS_MSG_REPLY;
        reply.retcode = (id >= 0) ? 0 : -1;
        reply.window_id = id;
        ipc_mqueue_send(rp->ip_messages, &reply, sizeof(reply));
    }
}

static void handle_window_destroy(const uint8_t *buf)
{
    const dps_window_destroy_msg_t *msg = (const dps_window_destroy_msg_t *)buf;
    int id = msg->window_id;
    if (id >= 0 && id < DPS_MAX_WINDOWS) {
        windows[id].active = 0;
        z_remove(id);
    }
}

static void handle_window_move(const uint8_t *buf)
{
    const dps_window_move_msg_t *msg = (const dps_window_move_msg_t *)buf;
    int id = msg->window_id;
    if (id >= 0 && id < DPS_MAX_WINDOWS && windows[id].active) {
        windows[id].x = msg->x;
        windows[id].y = msg->y;
    }
}

static void handle_window_resize(const uint8_t *buf)
{
    const dps_window_resize_msg_t *msg = (const dps_window_resize_msg_t *)buf;
    int id = msg->window_id;
    if (id >= 0 && id < DPS_MAX_WINDOWS && windows[id].active) {
        windows[id].w = msg->w;
        windows[id].h = msg->h;
    }
}

static void handle_draw_rect(const uint8_t *buf)
{
    const dps_draw_rect_msg_t *msg = (const dps_draw_rect_msg_t *)buf;
    window_draw_rect(msg->window_id, msg->x, msg->y, msg->w, msg->h,
                     msg->fg, msg->bg);
}

static void handle_draw_text(const uint8_t *buf)
{
    const dps_draw_text_msg_t *msg = (const dps_draw_text_msg_t *)buf;
    window_draw_text(msg->window_id, msg->x, msg->y, msg->text,
                     msg->fg, msg->bg);
}

static void dispatch_ipc(const uint8_t *buf)
{
    mach_msg_header_t *hdr = (mach_msg_header_t *)buf;

    switch (hdr->msgh_id) {
    case DPS_MSG_WINDOW_CREATE:  handle_window_create(buf);  break;
    case DPS_MSG_WINDOW_DESTROY: handle_window_destroy(buf); break;
    case DPS_MSG_WINDOW_MOVE:    handle_window_move(buf);    break;
    case DPS_MSG_WINDOW_RESIZE:  handle_window_resize(buf);  break;
    case DPS_MSG_DRAW_RECT:      handle_draw_rect(buf);      break;
    case DPS_MSG_DRAW_TEXT:       handle_draw_text(buf);      break;
    case DPS_MSG_FLUSH:          compositor_redraw_all();     break;
    default: break;
    }
}

/* -------------------------------------------------------------------------
 * Display server main loop (kernel thread entry point)
 * ------------------------------------------------------------------------- */

void display_server_main(void)
{
    serial_putstr("[display] starting display server...\r\n");

    while (!bootstrap_port)
        sched_yield();

    struct task *ktask = kernel_task_ptr();
    display_port = ipc_port_alloc(ktask);
    if (!display_port) {
        serial_putstr("[display] FAIL — could not allocate port\r\n");
        for (;;) sched_sleep();
    }

    bootstrap_register_msg_t reg;
    kmemset(&reg, 0, sizeof(reg));
    reg.hdr.msgh_size = sizeof(reg);
    reg.hdr.msgh_id = BOOTSTRAP_MSG_REGISTER;
    kstrncpy(reg.name, "com.unhox.display", BOOTSTRAP_NAME_MAX);
    reg.service_port = (uint64_t)display_port;
    ipc_mqueue_send(bootstrap_port->ip_messages, &reg, sizeof(reg));

    compositor_init();
    compositor_redraw_all();

    serial_putstr("[display] initialized — registered com.unhox.display\r\n");

    /*
     * Event loop: alternate between draining IPC messages (non-blocking)
     * and polling the PS/2 mouse. Yield when idle to avoid burning CPU.
     */
    uint8_t buf[256];
    mach_msg_size_t out_size = 0;

    for (;;) {
        /* Drain all pending IPC messages */
        int got_msg = 0;
        for (;;) {
            mach_msg_return_t mr = ipc_mqueue_receive(
                display_port->ip_messages, buf, sizeof(buf), &out_size,
                0 /* non-blocking */);
            if (mr != MACH_MSG_SUCCESS)
                break;
            dispatch_ipc(buf);
            got_msg = 1;
        }

        /* Poll mouse */
        handle_mouse();

        /* If nothing happened, yield to avoid spinning */
        if (!got_msg)
            sched_yield();
    }
}
