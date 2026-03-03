/*
 * kernel/device/vga_text.h — VGA text mode driver for UNHOX
 *
 * Provides 80x25 text mode output using the legacy VGA text buffer
 * at physical address 0xB8000. This is always available on PC-compatible
 * hardware and requires no special initialization.
 *
 * Text format: 16-bit cells, each containing:
 *   [7:0]   ASCII character
 *   [11:8]  foreground color (0-15)
 *   [14:12] background color (0-7)
 *   [15]    blink bit
 *
 * Reference: OSDev wiki "Text Mode Cursor", "VGA Text Mode"
 */

#ifndef VGA_TEXT_H
#define VGA_TEXT_H

#include <stdint.h>

/* VGA text mode dimensions */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* VGA colors (4-bit) */
typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE,
    VGA_COLOR_GREEN,
    VGA_COLOR_CYAN,
    VGA_COLOR_RED,
    VGA_COLOR_MAGENTA,
    VGA_COLOR_BROWN,
    VGA_COLOR_LIGHT_GREY,
    VGA_COLOR_DARK_GREY,
    VGA_COLOR_LIGHT_BLUE,
    VGA_COLOR_LIGHT_GREEN,
    VGA_COLOR_LIGHT_CYAN,
    VGA_COLOR_LIGHT_RED,
    VGA_COLOR_LIGHT_MAGENTA,
    VGA_COLOR_LIGHT_BROWN,
    VGA_COLOR_WHITE
} vga_color_t;

/*
 * vga_init — initialize VGA text mode driver.
 *
 * Clears the screen and positions cursor at (0, 0).
 */
void vga_init(void);

/*
 * vga_clear — clear screen with specified colors.
 */
void vga_clear(vga_color_t fg, vga_color_t bg);

/*
 * vga_putchar — write character at current cursor position.
 *
 * Handles newline (\n) and scrolling when reaching bottom of screen.
 */
void vga_putchar(char c, vga_color_t fg, vga_color_t bg);

/*
 * vga_putstr — write string at current cursor position.
 */
void vga_putstr(const char *str, vga_color_t fg, vga_color_t bg);

/*
 * vga_write_at — write character at specific position.
 */
void vga_write_at(uint8_t x, uint8_t y, char c, vga_color_t fg, vga_color_t bg);

/*
 * vga_set_cursor — move cursor to specified position.
 */
void vga_set_cursor(uint8_t x, uint8_t y);

/*
 * vga_test — diagnostic test showing color palette and text.
 */
void vga_test(void);

#endif /* VGA_TEXT_H */
