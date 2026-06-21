// keyboard.h - PS/2 AZERTY keyboard driver

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Special key codes returned by keyboard_getchar() for non-printable keys
#define KEY_NONE        0x00
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_ESCAPE      0x1B
#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_F1          0x90
#define KEY_DELETE      0x7F

// Initialize keyboard driver and install IRQ1 handler
void keyboard_init(void);

// Returns the last key pressed (0 if none pending), then clears it.
// Returns printable ASCII or one of the KEY_* constants above.
char keyboard_getchar(void);

// Returns 1 if a key is available in the buffer
int keyboard_has_key(void);

// IRQ1 handler — called from isr_stubs.asm
void irq1_handler(void);

#endif // KEYBOARD_H
