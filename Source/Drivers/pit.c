// pit.c - Intel 8253/8254 PIT driver
//
// Channel 0 → IRQ0, used for the system tick.
// Mode 3 (square wave) with automatic reload lets the counter fire at a
// constant rate set by the divisor:  divisor = PIT_BASE_FREQ / desired_hz

#include "pit.h"
#include "pic.h"
#include "io.h"
#include "../Krnl/cpu/isr.h"

// ---------------------------------------------------------------------------
// Internal tick counter (volatile: modified inside the IRQ handler)
// ---------------------------------------------------------------------------

static volatile uint64_t tick_count = 0;
static uint32_t ticks_per_ms = 0;

// ---------------------------------------------------------------------------
// pit_init — configure channel 0 and install on IRQ 0
// ---------------------------------------------------------------------------

void pit_init(uint32_t freq_hz)
{
    if (freq_hz == 0) freq_hz = 1;

    uint32_t divisor = PIT_BASE_FREQ / freq_hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;   // clamp to 16-bit max
    if (divisor < 1)      divisor = 1;

    ticks_per_ms = freq_hz / 1000;
    if (ticks_per_ms == 0) ticks_per_ms = 1;

    // Command byte:
    //   bits 7-6 = 00  → channel 0
    //   bits 5-4 = 11  → access mode: low byte then high byte
    //   bits 3-1 = 011 → mode 3 (square wave generator)
    //   bit  0   = 0   → binary counting
    outb(PIT_CMD, 0x36);

    // Write divisor low byte then high byte
    outb(PIT_CHAN0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHAN0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    // IRQ 0 handler is installed by the caller (main.c) via irq_install()
}

// ---------------------------------------------------------------------------
// pit_get_ticks
// ---------------------------------------------------------------------------

uint64_t pit_get_ticks(void)
{
    return tick_count;
}

// ---------------------------------------------------------------------------
// pit_sleep_ms — busy-wait (interrupts must be enabled)
// ---------------------------------------------------------------------------

void pit_sleep_ms(uint32_t ms)
{
    uint64_t target = tick_count + (uint64_t)ms * ticks_per_ms;
    while (tick_count < target)
    {
        __asm__ volatile ("hlt");   // wait for the next interrupt
    }
}

// ---------------------------------------------------------------------------
// irq0_pit_tick — called from sched.c's irq0_tick_handler to tick counters
// ---------------------------------------------------------------------------

void irq0_pit_tick(void)
{
    tick_count++;
    isr_increment_counter(32);
}

// ---------------------------------------------------------------------------
// irq0_handler — called from the assembly stub every tick.
// NOTE: The assembly stub (irq0 in isr_stubs.asm) uses the context-switching
// pattern.  It calls irq0_tick_handler (declared in sched.h) directly,
// NOT this function.  This function exists only to satisfy the pit.h header
// declaration (which is kept for back-compat).  The REAL handler is:
//   uint64_t irq0_tick_handler(uint64_t current_rsp)  -- in sched.c
// which increments the tick counter, sends EOI, then calls schedule().
// ---------------------------------------------------------------------------

void irq0_handler(void)
{
    // Should not be reached; the asm stub calls irq0_tick_handler directly.
    tick_count++;
    isr_increment_counter(32);
    pic_send_eoi(0);
}
