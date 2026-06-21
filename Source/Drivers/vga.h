// vga.h - VGA text mode 80x25 driver

#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

// VGA text mode dimensions
#define VGA_COLS    80
#define VGA_ROWS    25

// VGA MMIO buffer address
#define VGA_BUFFER  ((volatile uint16_t *)0xB8000)

// VGA color constants (foreground / background)
typedef enum
{
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW        = 14,
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

// Initialize VGA (clear screen, set default color, reset cursor)
void vga_init(void);

// Clear the entire screen with the current background color
void vga_clear(void);

// Set foreground and background color for subsequent output
void vga_set_color(vga_color_t fg, vga_color_t bg);

// Write a single character at the current cursor position
void vga_putchar(char c);

// Write a null-terminated string
void vga_puts(const char *str);

// Move the hardware cursor to (col, row)
void vga_set_cursor(uint8_t col, uint8_t row);

// Get current cursor column / row
uint8_t vga_get_col(void);
uint8_t vga_get_row(void);

// Write a hex uint64 for debug purposes
void vga_put_hex(uint64_t value);

// Write a decimal uint64
void vga_put_dec(uint64_t value);

// Scroll operations
void vga_scroll_viewport(int delta);
void vga_scroll_viewport_to_bottom(void);

#endif // VGA_H
