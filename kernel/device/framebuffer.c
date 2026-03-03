/*
 * kernel/device/framebuffer.c — Framebuffer driver implementation for UNHOX
 *
 * Provides basic 2D graphics primitives for a linear RGB framebuffer.
 * Initialized from Multiboot1 video mode info.
 *
 * Memory mapping strategy:
 *   - Framebuffer physical address is provided by bootloader
 *   - We identity-map it into kernel virtual space (for now)
 *   - Future: proper VM mapping via vm_map_device()
 *
 * Reference: Multiboot Specification v0.6.96 §3.3
 */

#include "framebuffer.h"
#include "kern/multiboot.h"
#include "kern/kalloc.h"
#include "platform/platform.h"

/* Serial output for diagnostics */
extern void serial_putstr(const char *s);
extern void serial_puthex(uint64_t val);
extern void serial_putdec(uint32_t val);

/* Framebuffer state */
static struct framebuffer_info fb_info;
static uint8_t *fb_base = NULL;
static int fb_initialized = 0;

/*
 * Pack RGB color into pixel value based on framebuffer format.
 */
static inline uint32_t fb_pack_color(fb_color_t color)
{
    uint32_t r = ((uint32_t)color.r >> (8 - fb_info.red_size)) << fb_info.red_pos;
    uint32_t g = ((uint32_t)color.g >> (8 - fb_info.green_size)) << fb_info.green_pos;
    uint32_t b = ((uint32_t)color.b >> (8 - fb_info.blue_size)) << fb_info.blue_pos;
    return r | g | b;
}

/*
 * Write pixel value to framebuffer at (x, y).
 * No bounds checking — caller must ensure x < width && y < height.
 */
static inline void fb_write_pixel(uint32_t x, uint32_t y, uint32_t pixel)
{
    uint8_t *addr = fb_base + (y * fb_info.pitch) + (x * (fb_info.bpp / 8));

    switch (fb_info.bpp) {
    case 32:
        *(uint32_t *)addr = pixel;
        break;
    case 24:
        addr[0] = (uint8_t)(pixel & 0xFF);
        addr[1] = (uint8_t)((pixel >> 8) & 0xFF);
        addr[2] = (uint8_t)((pixel >> 16) & 0xFF);
        break;
    case 16:
        *(uint16_t *)addr = (uint16_t)pixel;
        break;
    default:
        /* Unsupported BPP — no-op */
        break;
    }
}

int fb_init(uint32_t mb_info_phys)
{
    serial_putstr("[framebuffer] initializing\r\n");

    struct multiboot_info *mbinfo =
        (struct multiboot_info *)(uint64_t)mb_info_phys;

    if (!mbinfo) {
        serial_putstr("[framebuffer] ERROR: NULL multiboot info\r\n");
        return -1;
    }

    /* Check if framebuffer info is present */
    if (!(mbinfo->flags & MULTIBOOT_INFO_FRAMEBUFFER)) {
        serial_putstr("[framebuffer] ERROR: no framebuffer info from bootloader\r\n");
        serial_putstr("[framebuffer] HINT: boot with -vga std or add video= options\r\n");
        return -1;
    }

    /* Extract framebuffer parameters */
    fb_info.phys_addr   = mbinfo->framebuffer_addr;
    fb_info.width       = mbinfo->framebuffer_width;
    fb_info.height      = mbinfo->framebuffer_height;
    fb_info.pitch       = mbinfo->framebuffer_pitch;
    fb_info.bpp         = mbinfo->framebuffer_bpp;

    /* Check for supported RGB mode */
    if (mbinfo->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        serial_putstr("[framebuffer] ERROR: unsupported framebuffer type: ");
        serial_putdec(mbinfo->framebuffer_type);
        serial_putstr("\r\n");
        return -1;
    }

    /* Extract RGB field positions */
    fb_info.red_pos     = mbinfo->framebuffer_red_field_position;
    fb_info.red_size    = mbinfo->framebuffer_red_mask_size;
    fb_info.green_pos   = mbinfo->framebuffer_green_field_position;
    fb_info.green_size  = mbinfo->framebuffer_green_mask_size;
    fb_info.blue_pos    = mbinfo->framebuffer_blue_field_position;
    fb_info.blue_size   = mbinfo->framebuffer_blue_mask_size;

    /* Validate parameters */
    if (fb_info.width == 0 || fb_info.height == 0 || fb_info.bpp < 16) {
        serial_putstr("[framebuffer] ERROR: invalid resolution or BPP\r\n");
        return -1;
    }

    /*
     * Map framebuffer into kernel virtual address space.
     * For now, we use identity mapping (phys addr == virtual addr).
     * This works because QEMU's framebuffer is above 1MB and our early
     * page tables identity-map low memory.
     *
     * TODO: Use proper vm_map_device() once VM subsystem supports it.
     */
    fb_base = (uint8_t *)fb_info.phys_addr;

    fb_initialized = 1;

    /* Log configuration */
    serial_putstr("[framebuffer] base:       ");
    serial_puthex(fb_info.phys_addr);
    serial_putstr("\r\n");

    serial_putstr("[framebuffer] resolution: ");
    serial_putdec(fb_info.width);
    serial_putstr("x");
    serial_putdec(fb_info.height);
    serial_putstr("\r\n");

    serial_putstr("[framebuffer] bpp:        ");
    serial_putdec(fb_info.bpp);
    serial_putstr("\r\n");

    serial_putstr("[framebuffer] pitch:      ");
    serial_putdec(fb_info.pitch);
    serial_putstr("\r\n");

    serial_putstr("[framebuffer] RGB format: R");
    serial_putdec(fb_info.red_size);
    serial_putstr("@");
    serial_putdec(fb_info.red_pos);
    serial_putstr(" G");
    serial_putdec(fb_info.green_size);
    serial_putstr("@");
    serial_putdec(fb_info.green_pos);
    serial_putstr(" B");
    serial_putdec(fb_info.blue_size);
    serial_putstr("@");
    serial_putdec(fb_info.blue_pos);
    serial_putstr("\r\n");

    serial_putstr("[framebuffer] ready\r\n");
    return 0;
}

const struct framebuffer_info *fb_get_info(void)
{
    return fb_initialized ? &fb_info : NULL;
}

void fb_clear(fb_color_t color)
{
    if (!fb_initialized)
        return;

    uint32_t pixel = fb_pack_color(color);
    for (uint32_t y = 0; y < fb_info.height; y++) {
        for (uint32_t x = 0; x < fb_info.width; x++) {
            fb_write_pixel(x, y, pixel);
        }
    }
}

void fb_plot(uint32_t x, uint32_t y, fb_color_t color)
{
    if (!fb_initialized || x >= fb_info.width || y >= fb_info.height)
        return;

    uint32_t pixel = fb_pack_color(color);
    fb_write_pixel(x, y, pixel);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  fb_color_t color)
{
    if (!fb_initialized)
        return;

    /* Clip to framebuffer bounds */
    if (x >= fb_info.width || y >= fb_info.height)
        return;

    if (x + w > fb_info.width)
        w = fb_info.width - x;
    if (y + h > fb_info.height)
        h = fb_info.height - y;

    uint32_t pixel = fb_pack_color(color);
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            fb_write_pixel(x + dx, y + dy, pixel);
        }
    }
}

void fb_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  fb_color_t color)
{
    if (!fb_initialized)
        return;

    /* Bresenham's line algorithm */
    int dx = (int)x1 - (int)x0;
    int dy = (int)y1 - (int)y0;

    int dx_abs = dx < 0 ? -dx : dx;
    int dy_abs = dy < 0 ? -dy : dy;

    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;

    int err = dx_abs - dy_abs;

    int x = (int)x0;
    int y = (int)y0;

    for (;;) {
        fb_plot((uint32_t)x, (uint32_t)y, color);

        if (x == (int)x1 && y == (int)y1)
            break;

        int e2 = 2 * err;

        if (e2 > -dy_abs) {
            err -= dy_abs;
            x += sx;
        }

        if (e2 < dx_abs) {
            err += dx_abs;
            y += sy;
        }
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  fb_color_t color)
{
    if (!fb_initialized || w == 0 || h == 0)
        return;

    /* Top edge */
    fb_draw_line(x, y, x + w - 1, y, color);
    /* Bottom edge */
    fb_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    /* Left edge */
    fb_draw_line(x, y, x, y + h - 1, color);
    /* Right edge */
    fb_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void fb_test(void)
{
    if (!fb_initialized) {
        serial_putstr("[framebuffer] test SKIP — not initialized\r\n");
        return;
    }

    serial_putstr("[framebuffer] test starting\r\n");

    /* Clear screen to black */
    fb_clear(FB_COLOR_BLACK);

    /* Draw color bars (top 1/3 of screen) */
    uint32_t bar_width = fb_info.width / 8;
    uint32_t bar_height = fb_info.height / 3;

    fb_fill_rect(0 * bar_width, 0, bar_width, bar_height, FB_COLOR_RED);
    fb_fill_rect(1 * bar_width, 0, bar_width, bar_height, FB_COLOR_GREEN);
    fb_fill_rect(2 * bar_width, 0, bar_width, bar_height, FB_COLOR_BLUE);
    fb_fill_rect(3 * bar_width, 0, bar_width, bar_height, FB_COLOR_YELLOW);
    fb_fill_rect(4 * bar_width, 0, bar_width, bar_height, FB_COLOR_CYAN);
    fb_fill_rect(5 * bar_width, 0, bar_width, bar_height, FB_COLOR_MAGENTA);
    fb_fill_rect(6 * bar_width, 0, bar_width, bar_height, FB_COLOR_WHITE);
    fb_fill_rect(7 * bar_width, 0, bar_width, bar_height, FB_COLOR_GRAY);

    /* Draw diagonal lines (middle 1/3) */
    uint32_t mid_start = bar_height;
    uint32_t mid_end = 2 * bar_height;
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t x_start = (fb_info.width * i) / 10;
        uint32_t x_end = (fb_info.width * (i + 1)) / 10;
        fb_color_t line_color = (i % 2) ? FB_COLOR_WHITE : FB_COLOR_CYAN;
        fb_draw_line(x_start, mid_start, x_end, mid_end, line_color);
    }

    /* Draw rectangles (bottom 1/3) */
    uint32_t rect_start = 2 * bar_height;
    fb_draw_rect(20, rect_start + 20, 100, 80, FB_COLOR_RED);
    fb_fill_rect(25, rect_start + 25, 90, 70, FB_COLOR_GREEN);

    fb_draw_rect(150, rect_start + 20, 100, 80, FB_COLOR_BLUE);
    fb_fill_rect(155, rect_start + 25, 90, 70, FB_COLOR_YELLOW);

    fb_draw_rect(280, rect_start + 20, 100, 80, FB_COLOR_MAGENTA);
    fb_fill_rect(285, rect_start + 25, 90, 70, FB_COLOR_CYAN);

    serial_putstr("[framebuffer] test PASS — test pattern displayed\r\n");
}
