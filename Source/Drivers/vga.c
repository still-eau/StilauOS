// vga.c - VGA text mode 80x25 driver with console history buffer
//
// The VGA text buffer is a flat array at 0xB8000.
// Each cell is 2 bytes: [low = character, high = attribute].
// Attribute byte: bits 7-4 = background color, bits 3-0 = foreground color.
//
// Hardware cursor is controlled via CRTC registers (port 0x3D4/0x3D5).

#include "vga.h"
#include "io.h"

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

#define CONSOLE_BUFFER_ROWS 300

static uint16_t console_buffer[CONSOLE_BUFFER_ROWS * VGA_COLS];
static int cursor_row = 0;
static int cursor_col = 0;
static int console_view_row = 0;
static uint8_t vga_attr = 0;   // packed attribute byte

// CRTC register ports
#define VGA_CRTC_CMD    0x3D4
#define VGA_CRTC_DATA   0x3D5
#define VGA_CRTC_CURSOR_HIGH    14
#define VGA_CRTC_CURSOR_LOW     15

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline uint8_t make_attr(vga_color_t fg, vga_color_t bg)
{
    return (uint8_t)((bg << 4) | (fg & 0x0F));
}

static inline uint16_t make_entry(char c, uint8_t attr)
{
    return (uint16_t)((uint16_t)attr << 8 | (uint8_t)c);
}

static void vga_refresh(void)
{
    volatile uint16_t *real_buf = VGA_BUFFER;
    int src_offset = console_view_row * VGA_COLS;

    // Redraw screen content from virtual buffer
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
    {
        real_buf[i] = console_buffer[src_offset + i];
    }

    // Update CRTC hardware cursor position
    int screen_row = cursor_row - console_view_row;
    if (screen_row >= 0 && screen_row < VGA_ROWS)
    {
        uint16_t pos = (uint16_t)(screen_row * VGA_COLS + cursor_col);
        outb(VGA_CRTC_CMD,  VGA_CRTC_CURSOR_HIGH);
        outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
        outb(VGA_CRTC_CMD,  VGA_CRTC_CURSOR_LOW);
        outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    }
    else
    {
        // Move hardware cursor off-screen to hide it
        uint16_t pos = (uint16_t)(VGA_ROWS * VGA_COLS);
        outb(VGA_CRTC_CMD,  VGA_CRTC_CURSOR_HIGH);
        outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));
        outb(VGA_CRTC_CMD,  VGA_CRTC_CURSOR_LOW);
        outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void vga_init(void)
{
    vga_attr = make_attr(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    cursor_col = 0;
    cursor_row = 0;
    console_view_row = 0;

    // Fill virtual console buffer with blanks
    uint16_t blank = make_entry(' ', vga_attr);
    for (int i = 0; i < CONSOLE_BUFFER_ROWS * VGA_COLS; i++)
    {
        console_buffer[i] = blank;
    }

    vga_refresh();
}

void vga_clear(void)
{
    uint16_t blank = make_entry(' ', vga_attr);
    for (int i = 0; i < CONSOLE_BUFFER_ROWS * VGA_COLS; i++)
    {
        console_buffer[i] = blank;
    }

    cursor_col = 0;
    cursor_row = 0;
    console_view_row = 0;
    vga_refresh();
}

void vga_set_color(vga_color_t fg, vga_color_t bg)
{
    vga_attr = make_attr(fg, bg);
}

void vga_putchar(char c)
{
    // Snap scroll back to bottom on output
    int max_view = cursor_row - VGA_ROWS + 1;
    if (max_view < 0) max_view = 0;
    console_view_row = max_view;

    if (c == '\n')
    {
        cursor_col = 0;
        cursor_row++;
    }
    else if (c == '\r')
    {
        cursor_col = 0;
    }
    else if (c == '\t')
    {
        cursor_col = (cursor_col + 4) & ~3;
    }
    else if (c == '\b')
    {
        if (cursor_col > 0)
        {
            cursor_col--;
        }
        else if (cursor_row > 0)
        {
            cursor_row--;
            cursor_col = VGA_COLS - 1;
        }
        console_buffer[cursor_row * VGA_COLS + cursor_col] = make_entry(' ', vga_attr);
    }
    else
    {
        console_buffer[cursor_row * VGA_COLS + cursor_col] = make_entry(c, vga_attr);
        cursor_col++;
    }

    // Wrap at right edge
    if (cursor_col >= VGA_COLS)
    {
        cursor_col = 0;
        cursor_row++;
    }

    // Scroll virtual buffer if past the end of CONSOLE_BUFFER_ROWS
    if (cursor_row >= CONSOLE_BUFFER_ROWS)
    {
        // Shift all rows up by one in console_buffer
        for (int r = 1; r < CONSOLE_BUFFER_ROWS; r++)
        {
            for (int col = 0; col < VGA_COLS; col++)
            {
                console_buffer[(r - 1) * VGA_COLS + col] = console_buffer[r * VGA_COLS + col];
            }
        }
        // Clear the new bottom row
        uint16_t blank = make_entry(' ', vga_attr);
        for (int col = 0; col < VGA_COLS; col++)
        {
            console_buffer[(CONSOLE_BUFFER_ROWS - 1) * VGA_COLS + col] = blank;
        }
        cursor_row = CONSOLE_BUFFER_ROWS - 1;
    }

    // Update view row after potentially wrapping/scrolling
    max_view = cursor_row - VGA_ROWS + 1;
    if (max_view < 0) max_view = 0;
    console_view_row = max_view;

    vga_refresh();
}

void vga_puts(const char *str)
{
    while (*str)
    {
        vga_putchar(*str++);
    }
}

void vga_set_cursor(uint8_t col, uint8_t row)
{
    if (col < VGA_COLS && row < VGA_ROWS)
    {
        cursor_col = col;
        cursor_row = console_view_row + row;
        vga_refresh();
    }
}

uint8_t vga_get_col(void) { return cursor_col; }

uint8_t vga_get_row(void)
{
    int screen_row = cursor_row - console_view_row;
    if (screen_row < 0) return 0;
    if (screen_row >= VGA_ROWS) return VGA_ROWS - 1;
    return (uint8_t)screen_row;
}

void vga_put_hex(uint64_t value)
{
    const char hex[] = "0123456789ABCDEF";
    char       buf[19];   // "0x" + 16 digits + '\0'

    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = '\0';

    // Fill from the right
    for (int j = 17; j >= 2; j--)
    {
        buf[j] = hex[value & 0xF];
        value >>= 4;
    }

    vga_puts(buf);
}

void vga_put_dec(uint64_t value)
{
    char  buf[21];
    int   i = 20;

    buf[20] = '\0';

    if (value == 0)
    {
        vga_putchar('0');
        return;
    }

    while (value > 0 && i > 0)
    {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }

    vga_puts(&buf[i]);
}

// ---------------------------------------------------------------------------
// Scroll operations
// ---------------------------------------------------------------------------

void vga_scroll_viewport(int delta)
{
    int max_view = cursor_row - VGA_ROWS + 1;
    if (max_view < 0) max_view = 0;

    console_view_row += delta;

    if (console_view_row < 0)
    {
        console_view_row = 0;
    }
    if (console_view_row > max_view)
    {
        console_view_row = max_view;
    }

    vga_refresh();
}

void vga_scroll_viewport_to_bottom(void)
{
    int max_view = cursor_row - VGA_ROWS + 1;
    if (max_view < 0) max_view = 0;
    if (console_view_row != max_view)
    {
        console_view_row = max_view;
        vga_refresh();
    }
}
