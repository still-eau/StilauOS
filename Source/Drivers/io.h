// io.h - Inline x86 port I/O helpers
//
// These are used by all drivers that talk to hardware via IN/OUT instructions.

#ifndef IO_H
#define IO_H

#include <stdint.h>

// Write a byte to an I/O port
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port) : "memory");
}

// Read a byte from an I/O port
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

// Write a word (16-bit) to an I/O port
static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(port) : "memory");
}

// Read a word (16-bit) from an I/O port
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

// Small I/O delay (gives old hardware time to react)
static inline void io_wait(void)
{
    // Port 0x80 is used for POST codes — writing to it causes ~1 µs delay
    __asm__ volatile ("outb %%al, $0x80" :: "a"(0) : "memory");
}

#endif // IO_H
