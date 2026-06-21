// console.c - Kernel console: VGA output + PS/2 keyboard input
//
// Directs all characters to both the VGA screen and the serial port (COM1).
//
// Comments use only standard ASCII characters.

#include "console.h"
#include "io.h"
#include "vga.h"
#include "keyboard.h"

#include <stdarg.h>
#include <stdint.h>

void kconsole_init(void)
{
    vga_init();
}

// Print a character to both COM1 serial port and VGA screen
void kconsole_putchar(char c)
{
    outb(0x3F8, c);
    vga_putchar(c);
}

// Print a string to both COM1 serial port and VGA screen
void kconsole_puts(const char *str)
{
    while (*str)
    {
        kconsole_putchar(*str++);
    }
}

// Print a string and a newline to both COM1 serial port and VGA screen
void kprintln(const char *str)
{
    kconsole_puts(str);
    kconsole_putchar('\n');
}

void kconsole_clear(void)
{
    vga_clear();
}

// Integer-to-string conversion helpers using kconsole functions
static void print_uint_base(uint64_t value, uint8_t base, int uppercase)
{
    const char *digits_lc = "0123456789abcdef";
    const char *digits_uc = "0123456789ABCDEF";
    const char *digits    = uppercase ? digits_uc : digits_lc;

    char  buf[66];
    int   i = 64;
    buf[65] = '\0';

    if (value == 0)
    {
        kconsole_putchar('0');
        return;
    }

    while (value > 0 && i >= 0)
    {
        buf[i--] = digits[value % base];
        value    /= base;
    }

    kconsole_puts(&buf[i + 1]);
}

static void print_int(int64_t value)
{
    if (value < 0)
    {
        kconsole_putchar('-');
        print_uint_base((uint64_t)(-(value + 1)) + 1, 10, 0);
    }
    else
    {
        print_uint_base((uint64_t)value, 10, 0);
    }
}

// kprintf - minimal kernel printf routing output to both VGA and Serial
void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt)
    {
        if (*fmt != '%')
        {
            kconsole_putchar(*fmt++);
            continue;
        }

        fmt++; // skip '%'

        switch (*fmt)
        {
            case 'c':
                kconsole_putchar((char)va_arg(args, int));
                break;

            case 's':
            {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                kconsole_puts(s);
                break;
            }

            case 'd':
                print_int((int64_t)va_arg(args, int));
                break;

            case 'u':
                print_uint_base((uint64_t)va_arg(args, unsigned int), 10, 0);
                break;

            case 'x':
                print_uint_base((uint64_t)va_arg(args, unsigned int), 16, 0);
                break;

            case 'X':
                print_uint_base((uint64_t)va_arg(args, unsigned int), 16, 1);
                break;

            case 'p':
                kconsole_puts("0x");
                print_uint_base((uint64_t)(uintptr_t)va_arg(args, void *), 16, 0);
                break;

            case '%':
                kconsole_putchar('%');
                break;

            case '\0':
                goto done;

            default:
                kconsole_putchar('%');
                kconsole_putchar(*fmt);
                break;
        }

        fmt++;
    }

done:
    va_end(args);
}

// kconsole_readline - Line editor with cursor and backspace support
int kconsole_readline(char *buf, int len)
{
    int pos = 0;
    int end = 0;

    uint8_t start_col = vga_get_col();
    uint8_t start_row = vga_get_row();

    while (1)
    {
        while (!keyboard_has_key())
        {
            __asm__ volatile ("hlt");
        }

        unsigned char c = keyboard_getchar();

        vga_scroll_viewport_to_bottom();

        if (c == KEY_ENTER || c == '\n')
        {
            buf[end] = '\0';
            kconsole_putchar('\n');
            return end;
        }
        else if (c == KEY_BACKSPACE || c == '\b')
        {
            if (pos > 0)
            {
                for (int i = pos - 1; i < end - 1; i++)
                {
                    buf[i] = buf[i + 1];
                }
                pos--;
                end--;
                buf[end] = '\0';

                vga_set_cursor(start_col, start_row);
                for (int i = 0; i < end; i++)
                {
                    vga_putchar(buf[i]);
                }
                vga_putchar(' '); // erase last char
                vga_set_cursor((uint8_t)(start_col + pos), start_row);
            }
        }
        else if (c == KEY_LEFT)
        {
            if (pos > 0)
            {
                pos--;
                vga_set_cursor((uint8_t)(start_col + pos), start_row);
            }
        }
        else if (c == KEY_RIGHT)
        {
            if (pos < end)
            {
                pos++;
                vga_set_cursor((uint8_t)(start_col + pos), start_row);
            }
        }
        else if (c >= 0x20 || c == '\t')
        {
            if (end < len - 1)
            {
                for (int i = end; i > pos; i--)
                {
                    buf[i] = buf[i - 1];
                }
                buf[pos] = c;
                pos++;
                end++;
                buf[end] = '\0';

                vga_set_cursor((uint8_t)(start_col + pos - 1), start_row);
                for (int i = pos - 1; i < end; i++)
                {
                    vga_putchar(buf[i]);
                }
                vga_set_cursor((uint8_t)(start_col + pos), start_row);
            }
        }
    }
}

// Centered header banner
void kconsole_banner(const char *text)
{
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_CYAN);

    for (int i = 0; i < VGA_COLS; i++)
    {
        vga_putchar(' ');
    }

    uint8_t row = (uint8_t)(vga_get_row() - 1);

    int len = 0;
    const char *p = text;
    while (*p++) len++;

    uint8_t col = (uint8_t)((VGA_COLS - len) / 2);
    vga_set_cursor(col, row);
    vga_puts(text);

    vga_set_cursor(0, (uint8_t)(row + 1));
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
