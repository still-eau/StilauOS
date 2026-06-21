// keyboard.c - PS/2 AZERTY keyboard driver
//
// Reads make/break codes from port 0x60 on IRQ1.
// Maintains shift/caps state and translates to ASCII using AZERTY tables.
//
// Scancode Set 1 (default for PS/2 keyboards in PC-compatible BIOSes).

#include "keyboard.h"
#include "pic.h"
#include "io.h"
#include "../Krnl/cpu/isr.h"

// PS/2 data port
#define KB_DATA_PORT    0x60

// ---------------------------------------------------------------------------
// AZERTY scancode → ASCII tables (Scancode Set 1)
//
// Index = scancode (0x00 - 0x58).
// 0x00 = no character (unmapped or special key).
// ---------------------------------------------------------------------------

// Unshifted AZERTY layout
static const char azerty_unshifted[89] = {
/*00*/  0,     '\x1B', '&',   '\xE9', '"',   '\'',  '(',   '-',
/*08*/  '\xE8','_',    '\xE7','\xE0', ')',   '=',   '\b',  '\t',
/*10*/  'a',   'z',   'e',   'r',   't',   'y',   'u',   'i',
/*18*/  'o',   'p',   '^',   '$',   '\n',  0,     'q',   's',
/*20*/  'd',   'f',   'g',   'h',   'j',   'k',   'l',   'm',
/*28*/  '\xF9','`',   0,     '*',   'w',   'x',   'c',   'v',
/*30*/  'b',   'n',   ',',   ';',   ':',   '!',   0,     '*',
/*38*/  0,     ' ',   0,     0,     0,     0,     0,     0,
/*40*/  0,     0,     0,     0,     0,     0,     0,     '7',
/*48*/  '8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',
/*50*/  '2',   '3',   '0',   '.',   0,     0,     0,     0,
/*58*/  0
};

// Shifted AZERTY layout
static const char azerty_shifted[89] = {
/*00*/  0,     '\x1B', '1',   '2',   '3',   '4',   '5',   '6',
/*08*/  '7',   '8',   '9',   '0',   '\xB0','+'  , '\b',  '\t',
/*10*/  'A',   'Z',   'E',   'R',   'T',   'Y',   'U',   'I',
/*18*/  'O',   'P',   '\xA8','`',   '\n',  0,     'Q',   'S',
/*20*/  'D',   'F',   'G',   'H',   'J',   'K',   'L',   'M',
/*28*/  '%',   '~',   0,     '\xB5','W',   'X',   'C',   'V',
/*30*/  'B',   'N',   '?',   '.',   '/',   '\xA7',0,     '*',
/*38*/  0,     ' ',   0,     0,     0,     0,     0,     0,
/*40*/  0,     0,     0,     0,     0,     0,     0,     '7',
/*48*/  '8',   '9',   '-',   '4',   '5',   '6',   '+',   '1',
/*50*/  '2',   '3',   '0',   '.',   0,     0,     0,     0,
/*58*/  0
};

// ---------------------------------------------------------------------------
// Simple ring buffer (64 characters)
// ---------------------------------------------------------------------------

#define KB_BUF_SIZE 64

static volatile char   kb_buf[KB_BUF_SIZE];
static volatile int    kb_buf_head = 0;
static volatile int    kb_buf_tail = 0;

static void kb_buf_push(char c)
{
    int next = (kb_buf_head + 1) % KB_BUF_SIZE;
    if (next != kb_buf_tail)    // don't overwrite if full
    {
        kb_buf[kb_buf_head] = c;
        kb_buf_head = next;
    }
}

static char kb_buf_pop(void)
{
    if (kb_buf_tail == kb_buf_head) return 0;
    char c = kb_buf[kb_buf_tail];
    kb_buf_tail = (kb_buf_tail + 1) % KB_BUF_SIZE;
    return c;
}

// ---------------------------------------------------------------------------
// Modifier state
// ---------------------------------------------------------------------------

static volatile int shift_pressed = 0;
static volatile int caps_lock     = 0;
static volatile int ctrl_pressed  = 0;
static volatile int alt_pressed   = 0;
static volatile int extended      = 0;   // 0xE0 prefix received

// ---------------------------------------------------------------------------
// irq1_handler — reads one scancode byte per call
// ---------------------------------------------------------------------------

void irq1_handler(void)
{
    isr_increment_counter(33);
    uint8_t code = inb(KB_DATA_PORT);

    // Extended key prefix (arrows, Delete, etc.)
    if (code == 0xE0)
    {
        extended = 1;
        pic_send_eoi(1);
        return;
    }

    int is_break = (code & 0x80) != 0;     // break (key release) if bit 7 set
    uint8_t scan  = code & 0x7F;           // strip break bit

    if (extended)
    {
        extended = 0;
        if (!is_break)
        {
            switch (scan)
            {
                case 0x48: kb_buf_push(KEY_UP);    break;
                case 0x50: kb_buf_push(KEY_DOWN);  break;
                case 0x4B: kb_buf_push(KEY_LEFT);  break;
                case 0x4D: kb_buf_push(KEY_RIGHT); break;
                case 0x53: kb_buf_push(KEY_DELETE);break;
            }
        }
        pic_send_eoi(1);
        return;
    }

    // Handle modifiers
    switch (scan)
    {
        case 0x2A:  // Left Shift
        case 0x36:  // Right Shift
            shift_pressed = is_break ? 0 : 1;
            pic_send_eoi(1);
            return;

        case 0x3A:  // Caps Lock (toggle on make only)
            if (!is_break) caps_lock ^= 1;
            pic_send_eoi(1);
            return;

        case 0x1D:  // Ctrl
            ctrl_pressed = is_break ? 0 : 1;
            pic_send_eoi(1);
            return;

        case 0x38:  // Alt
            alt_pressed = is_break ? 0 : 1;
            pic_send_eoi(1);
            return;

        default:
            break;
    }

    // Only process make codes for printable keys
    if (!is_break && scan < 89)
    {
        int use_shift = shift_pressed;

        // CapsLock inverts shift for alphabetic keys (A-Z, a-z)
        if (caps_lock)
        {
            char base = azerty_unshifted[scan];
            if (base >= 'a' && base <= 'z') use_shift ^= 1;
        }

        char c = use_shift ? azerty_shifted[scan] : azerty_unshifted[scan];

        // Ctrl+key → control code (e.g. Ctrl+C = 0x03)
        if (ctrl_pressed && c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 1);
        else if (ctrl_pressed && c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 1);

        if (c != 0)
            kb_buf_push(c);
    }

    pic_send_eoi(1);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void keyboard_init(void)
{
    // Flush any stale byte from the PS/2 controller
    (void)inb(KB_DATA_PORT);

    // Install IRQ1 handler and unmask IRQ1
    extern void irq1(void);     // assembly stub in isr_stubs.asm
    // Use the idt_set_entry + pic_unmask_irq directly via pic.h irq_install
    extern void irq_install(uint8_t irq, void (*handler)(void));
    irq_install(1, irq1);
}

char keyboard_getchar(void)
{
    return kb_buf_pop();
}

int keyboard_has_key(void)
{
    return kb_buf_head != kb_buf_tail;
}
