/*
 * kernel/device/framebuffer.h — Framebuffer driver for UNHOX
 *
 * Provides a linear framebuffer interface initialized from Multiboot1
 * video mode information. Supports RGB direct color modes (24/32 bpp).
 *
 * The framebuffer is memory-mapped at the physical address provided by
 * the bootloader. Basic drawing primitives (plot pixel, fill rect) are
 * provided for early boot diagnostics and future GUI server integration.
 *
 * Reference: Multiboot Specification v0.6.96 §3.3 — Framebuffer Info
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

/* Framebuffer information extracted from Multiboot */
struct framebuffer_info {
    uint64_t    phys_addr;          /* Physical address of framebuffer */
    uint32_t    width;              /* Width in pixels */
    uint32_t    height;             /* Height in pixels */
    uint32_t    pitch;              /* Bytes per scanline */
    uint8_t     bpp;                /* Bits per pixel (16, 24, or 32) */
    uint8_t     red_pos;            /* Red field bit position */
    uint8_t     red_size;           /* Red field bit width */
    uint8_t     green_pos;          /* Green field bit position */
    uint8_t     green_size;         /* Green field bit width */
    uint8_t     blue_pos;           /* Blue field bit position */
    uint8_t     blue_size;          /* Blue field bit width */
};

/* Color representation (R, G, B each 0-255) */
typedef struct {
    uint8_t r, g, b;
} fb_color_t;

/* Common colors */
#define FB_COLOR_BLACK      ((fb_color_t){0, 0, 0})
#define FB_COLOR_WHITE      ((fb_color_t){255, 255, 255})
#define FB_COLOR_RED        ((fb_color_t){255, 0, 0})
#define FB_COLOR_GREEN      ((fb_color_t){0, 255, 0})
#define FB_COLOR_BLUE       ((fb_color_t){0, 0, 255})
#define FB_COLOR_YELLOW     ((fb_color_t){255, 255, 0})
#define FB_COLOR_CYAN       ((fb_color_t){0, 255, 255})
#define FB_COLOR_MAGENTA    ((fb_color_t){255, 0, 255})
#define FB_COLOR_GRAY       ((fb_color_t){128, 128, 128})

/*
 * fb_init — initialize framebuffer from Multiboot1 info.
 *
 * Extracts framebuffer parameters and maps the framebuffer into kernel
 * virtual address space. Must be called after VM initialization.
 *
 * mb_info_phys: physical address of Multiboot info structure
 * Returns: 0 on success, -1 if no framebuffer available
 */
int fb_init(uint32_t mb_info_phys);

/*
 * fb_get_info — retrieve framebuffer parameters.
 *
 * Returns a pointer to the internal framebuffer_info structure, or NULL
 * if the framebuffer is not initialized.
 */
const struct framebuffer_info *fb_get_info(void);

/*
 * fb_clear — fill entire screen with a solid color.
 */
void fb_clear(fb_color_t color);

/*
 * fb_plot — set pixel at (x, y) to the specified color.
 *
 * Coordinates are clipped to framebuffer bounds. No-op if out of range.
 */
void fb_plot(uint32_t x, uint32_t y, fb_color_t color);

/*
 * fb_fill_rect — fill a rectangle with a solid color.
 *
 * (x, y): top-left corner
 * w, h:   width and height in pixels
 *
 * Coordinates are clipped to framebuffer bounds.
 */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  fb_color_t color);

/*
 * fb_draw_line — draw a line from (x0, y0) to (x1, y1).
 *
 * Uses Bresenham's line algorithm.
 */
void fb_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  fb_color_t color);

/*
 * fb_draw_rect — draw a rectangle outline.
 *
 * (x, y): top-left corner
 * w, h:   width and height in pixels
 */
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  fb_color_t color);

/*
 * fb_test — diagnostic test routine.
 *
 * Draws test patterns to verify framebuffer operation.
 */
void fb_test(void);

#endif /* FRAMEBUFFER_H */
