/*
 * kernel/device/vga_text.c — VGA text mode driver implementation
 *
 * Uses the legacy VGA text buffer at 0xB8000 for 80x25 character output.
 *
 * Reference: OSDev wiki "VGA Text Mode"
 */

#include "vga_text.h"
#include "platform/ioport.h"

/* VGA text buffer physical address (identity-mapped in our setup) */
#define VGA_MEMORY      0xB8000

/* VGA I/O ports for cursor control */
#define VGA_CTRL_REG    0x3D4
#define VGA_DATA_REG    0x3D5

/* Cursor control commands */
#define VGA_CURSOR_LOC_HIGH  14
#define VGA_CURSOR_LOC_LOW   15

/* Current cursor position */
static uint8_t vga_row = 0;
static uint8_t vga_col = 0;

/* Pointer to VGA text buffer */
static volatile uint16_t *vga_buffer = (volatile uint16_t *)VGA_MEMORY;

/* Pack character and colors into a VGA cell */
static inline uint16_t vga_entry(char c, vga_color_t fg, vga_color_t bg)
{
    uint8_t color = (bg << 4) | (fg & 0x0F);
    return ((uint16_t)color << 8) | (uint8_t)c;
}

/* Update hardware cursor position */
static void vga_update_cursor(void)
{
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    
    outb(VGA_CTRL_REG, VGA_CURSOR_LOC_HIGH);
    outb(VGA_DATA_REG, (uint8_t)((pos >> 8) & 0xFF));
    
    outb(VGA_CTRL_REG, VGA_CURSOR_LOC_LOW);
    outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
}

/* Scroll screen up by one line */
static void vga_scroll(void)
{
    /* Move all lines up */
    for (uint8_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (uint8_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] =
                vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    
    /* Clear last line */
    uint16_t blank = vga_entry(' ', VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (uint8_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
    
    vga_row = VGA_HEIGHT - 1;
    vga_col = 0;
}

void vga_init(void)
{
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_set_cursor(0, 0);
}

void vga_clear(vga_color_t fg, vga_color_t bg)
{
    uint16_t blank = vga_entry(' ', fg, bg);
    
    for (uint16_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_putchar(char c, vga_color_t fg, vga_color_t bg)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else {
        uint16_t index = vga_row * VGA_WIDTH + vga_col;
        vga_buffer[index] = vga_entry(c, fg, bg);
        vga_col++;
    }
    
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    
    vga_update_cursor();
}

void vga_putstr(const char *str, vga_color_t fg, vga_color_t bg)
{
    while (*str) {
        vga_putchar(*str++, fg, bg);
    }
}

void vga_write_at(uint8_t x, uint8_t y, char c, vga_color_t fg, vga_color_t bg)
{
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;
    
    uint16_t index = y * VGA_WIDTH + x;
    vga_buffer[index] = vga_entry(c, fg, bg);
}

void vga_set_cursor(uint8_t x, uint8_t y)
{
    if (x >= VGA_WIDTH)
        x = VGA_WIDTH - 1;
    if (y >= VGA_HEIGHT)
        y = VGA_HEIGHT - 1;
    
    vga_row = y;
    vga_col = x;
    vga_update_cursor();
}

void vga_test(void)
{
    extern void serial_putstr(const char *);
    serial_putstr("[vga] test starting\r\n");
    
    /* Save current cursor position */
    uint8_t saved_row = vga_row;
    uint8_t saved_col = vga_col;
    
    /* Draw title */
    vga_set_cursor(25, 0);
    vga_putstr("=== UNHOX VGA TEST ===", VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLUE);
    
    /* Draw color palette */
    vga_set_cursor(0, 2);
    vga_putstr("Color palette:", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    for (uint8_t i = 0; i < 16; i++) {
        vga_set_cursor(i * 5, 3);
        char buf[4];
        buf[0] = (i < 10) ? ('0' + i) : ('A' + i - 10);
        buf[1] = ' ';
        buf[2] = 0xDB;  /* Block character */
        buf[3] = '\0';
        
        vga_putstr(buf, (vga_color_t)i, VGA_COLOR_BLACK);
    }
    
    /* Draw box */
    vga_set_cursor(0, 5);
    vga_putstr("+----------------------+", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    vga_set_cursor(0, 6);
    vga_putstr("|  VGA Text Mode OK!  |", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    vga_set_cursor(0, 7);
    vga_putstr("+----------------------+", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    /* Draw gradient */
    vga_set_cursor(30, 5);
    vga_putstr("Background gradient:", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (uint8_t i = 0; i < 8; i++) {
        vga_set_cursor(30 + i * 2, 6);
        vga_write_at(30 + i * 2, 6, ' ', VGA_COLOR_BLACK, (vga_color_t)i);
        vga_write_at(31 + i * 2, 6, ' ', VGA_COLOR_BLACK, (vga_color_t)i);
    }
    
    /* Restore cursor */
    vga_set_cursor(saved_col, saved_row);
    
    serial_putstr("[vga] test PASS — VGA text mode operational\r\n");
}
