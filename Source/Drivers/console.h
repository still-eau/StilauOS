// console.h - Kernel console: VGA output + PS/2 keyboard input
//
// Provides line editing with cursor movement and a minimal printf.
//
// Comments use only standard ASCII characters.

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "io.h"
#include <stddef.h>

// Initialize the console
void kconsole_init(void);

// Print a single character to both VGA and Serial
void kconsole_putchar(char c);

// Print a null-terminated string to both VGA and Serial
void kconsole_puts(const char *str);

// Print a string followed by a newline to both VGA and Serial
void kprintln(const char *str);

// Simple printf - supports: %c %s %d %u %x %p %%
void kprintf(const char *fmt, ...);

// Read a line from the keyboard
int kconsole_readline(char *buf, int len);

// Clear the console
void kconsole_clear(void);

// Print a colored banner line
void kconsole_banner(const char *text);

// Print a string to the serial port (maps to kconsole_puts so it shows on screen too)
static inline void k_serial_puts(const char *str) {
    kconsole_puts(str);
}

#endif // CONSOLE_H
