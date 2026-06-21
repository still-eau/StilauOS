// pit.h - Intel 8253/8254 Programmable Interval Timer driver

#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// PIT oscillator frequency: 1.193182 MHz
#define PIT_BASE_FREQ   1193182UL

// PIT I/O ports
#define PIT_CHAN0_DATA  0x40    // Channel 0 data port (IRQ0)
#define PIT_CHAN2_DATA  0x42    // Channel 2 data port (PC speaker)
#define PIT_CMD         0x43    // Mode/Command register

// Initialize the PIT to fire IRQ0 at the given frequency (Hz)
// Typical: pit_init(1000) → 1 ms tick (1000 Hz)
void pit_init(uint32_t freq_hz);

// Returns the number of ticks since pit_init() was called
uint64_t pit_get_ticks(void);

// Sleep for approximately `ms` milliseconds (busy-waits on tick counter)
void pit_sleep_ms(uint32_t ms);

// irq0_pit_tick() — increments tick_count and ISR counter.
// Called from sched.c's irq0_tick_handler (the actual IRQ0 C handler).
void irq0_pit_tick(void);

// IRQ0 handler (called from isr_stubs.asm — context-switching signature).
// Receives the current RSP (PUSH_ALL frame); must return next RSP.
// Do NOT call this directly from C code.
void irq0_handler(void);

#endif // PIT_H
