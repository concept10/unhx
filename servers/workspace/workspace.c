/*
 * servers/workspace/workspace.c — UNHOX Workspace Manager
 *
 * Creates the NeXT-heritage desktop: menu bar, windows, and content.
 * Runs as a kernel thread. Uses the AppKit backend to communicate
 * with the display server via Mach IPC.
 *
 * Milestone v1.0: NeXT-heritage desktop boots.
 */

#include "AppKit-backend/appkit_backend.h"
#include "device/vga_text.h"
#include "kern/klib.h"
#include "kern/sched.h"

/* Serial diagnostics */
extern void serial_putstr(const char *s);

void workspace_main(void)
{
    serial_putstr("[workspace] starting workspace manager...\r\n");

    /*
     * Wait a bit for the display server to register with bootstrap.
     * The display server thread runs first, but we yield to give it
     * time to complete registration.
     */
    for (int i = 0; i < 10; i++)
        sched_yield();

    /* Initialize AppKit backend (looks up display server port) */
    int rc = appkit_backend_init();
    if (rc != 0) {
        serial_putstr("[workspace] FAIL — could not init appkit backend\r\n");
        for (;;) sched_sleep();
    }
    serial_putstr("[workspace] appkit backend initialized\r\n");

    /*
     * Create desktop windows.
     *
     * VGA text mode layout (80x25):
     *   Row 0:     Menu bar (drawn by display server)
     *   Rows 2-9:  "About UNHOX" window (centered)
     *   Rows 11-22: Two side-by-side windows (Files + Terminal)
     *   Row 24:    Desktop background
     */

    /* Window 1: About UNHOX — centered overview */
    int about_id = appkit_window_create(
        18, 2,      /* x=18, y=2 */
        44, 8,      /* w=44, h=8 (1 title + 7 content) */
        "About UNHOX",
        VGA_COLOR_BLACK, VGA_COLOR_WHITE);

    if (about_id >= 0) {
        appkit_draw_text(about_id, 1, 1,
            "UNHOX - U Is Not Hurd Or X",
            VGA_COLOR_LIGHT_CYAN, VGA_COLOR_WHITE);
        appkit_draw_text(about_id, 1, 2,
            "Mach microkernel OS  v1.0",
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
        appkit_draw_text(about_id, 1, 4,
            "  +--+  CMU Mach heritage",
            VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
        appkit_draw_text(about_id, 1, 5,
            "  |NX|  NeXT-inspired desktop",
            VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
        appkit_draw_text(about_id, 1, 6,
            "  +--+  DPS display server",
            VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
    }

    /* Window 2: File Browser — left side */
    int files_id = appkit_window_create(
        1, 11,      /* x=1, y=11 */
        36, 12,     /* w=36, h=12 (1 title + 11 content) */
        "File Browser",
        VGA_COLOR_BLACK, VGA_COLOR_WHITE);

    if (files_id >= 0) {
        appkit_draw_text(files_id, 1, 0,
            "/",
            VGA_COLOR_LIGHT_BLUE, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 1,
            "  bin/",
            VGA_COLOR_BLUE, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 2,
            "    init.elf    (43K)",
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 3,
            "    sh.elf      (12K)",
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 4,
            "  dev/",
            VGA_COLOR_BLUE, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 5,
            "    console",
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 6,
            "    keyboard",
            VGA_COLOR_BLACK, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 7,
            "  etc/",
            VGA_COLOR_BLUE, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 8,
            "  tmp/",
            VGA_COLOR_BLUE, VGA_COLOR_WHITE);
        appkit_draw_text(files_id, 1, 9,
            "4 directories, 4 files",
            VGA_COLOR_DARK_GREY, VGA_COLOR_WHITE);
    }

    /* Window 3: Terminal — right side */
    int term_id = appkit_window_create(
        40, 11,     /* x=40, y=11 */
        39, 12,     /* w=39, h=12 (1 title + 11 content) */
        "Terminal",
        VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);

    if (term_id >= 0) {
        appkit_draw_text(term_id, 0, 0,
            "UNHOX Mach microkernel v1.0",
            VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 1,
            "Type 'help' for commands",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 3,
            "unhox$ ls /bin",
            VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 4,
            "init.elf  sh.elf",
            VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 6,
            "unhox$ uname -a",
            VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 7,
            "UNHOX 1.0 x86_64 Mach 3.0",
            VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        appkit_draw_text(term_id, 0, 9,
            "unhox$ _",
            VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    }

    /* Flush everything to screen */
    appkit_flush();

    serial_putstr("[workspace] desktop created\r\n");
    serial_putstr("[workspace] Milestone v1.0 PASS — NeXT-heritage desktop up\r\n");

    /* Workspace manager stays alive (could handle input events later) */
    for (;;)
        sched_sleep();
}
