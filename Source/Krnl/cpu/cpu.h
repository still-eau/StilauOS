// cpu.h - CPU subsystem init (GDT + IDT)

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

// Initialize the full CPU environment:
//   1. GDT (flat 64-bit segments)
//   2. IDT (all 256 gates, exception handlers)
void cpu_init(void);

// Read Model-Specific Register
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

// Write Model-Specific Register
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

// Pause the CPU until the next interrupt
static inline void cpu_halt(void)
{
    __asm__ volatile ("hlt");
}

// Disable interrupts
static inline void cpu_cli(void)
{
    __asm__ volatile ("cli");
}

// Enable interrupts
static inline void cpu_sti(void)
{
    __asm__ volatile ("sti");
}

#endif // CPU_H
