// isr.c - CPU Exception Handlers
//
// Each handler prints a panic message to VGA and halts.
// Exceptions that push an error code (8, 10-14, 17, 30) must account for it.

#include <stdint.h>
#include "isr.h"
#include "../../Drivers/vga.h"

// ---------------------------------------------------------------------------
// Global Interrupt Counters and Descriptions
// ---------------------------------------------------------------------------

static volatile uint64_t interrupt_counters[256] = {0};

void isr_increment_counter(int vector)
{
    if (vector >= 0 && vector < 256)
    {
        interrupt_counters[vector]++;
    }
}

uint64_t isr_get_count(int vector)
{
    if (vector >= 0 && vector < 256)
    {
        return interrupt_counters[vector];
    }
    return 0;
}

const char *isr_get_description(int vector)
{
    switch (vector)
    {
        case 0:  return "Division Error";
        case 1:  return "Debug Exception";
        case 2:  return "NMI Interrupt";
        case 3:  return "Breakpoint";
        case 4:  return "Overflow";
        case 5:  return "BOUND Range Exceeded";
        case 6:  return "Invalid Opcode";
        case 7:  return "Device Not Available";
        case 8:  return "Double Fault";
        case 9:  return "Coprocessor Segment Overrun";
        case 10: return "Invalid TSS";
        case 11: return "Segment Not Present";
        case 12: return "Stack-Segment Fault";
        case 13: return "General Protection Fault";
        case 14: return "Page Fault";
        case 15: return "Reserved Exception";
        case 16: return "x87 FPU Floating-Point Error";
        case 17: return "Alignment Check";
        case 18: return "Machine Check";
        case 19: return "SIMD Floating-Point Exception";
        case 20: return "Virtualization Exception";
        case 30: return "Security Exception";

        // IRQs
        case 32: return "IRQ 0 - PIT Timer";
        case 33: return "IRQ 1 - Keyboard";
        case 34: return "IRQ 2 - Cascade";
        case 35: return "IRQ 3 - COM2";
        case 36: return "IRQ 4 - COM1";
        case 37: return "IRQ 5 - LPT2";
        case 38: return "IRQ 6 - Floppy Disk";
        case 39: return "IRQ 7 - LPT1";
        case 40: return "IRQ 8 - RTC";
        case 41: return "IRQ 9 - Redirected";
        case 42: return "IRQ 10 - Reserved";
        case 43: return "IRQ 11 - Reserved";
        case 44: return "IRQ 12 - PS/2 Mouse";
        case 45: return "IRQ 13 - FPU Coprocessor";
        case 46: return "IRQ 14 - Primary ATA";
        case 47: return "IRQ 15 - Secondary ATA";

        default: return "Unknown / Mapped Software";
    }
}

// ---------------------------------------------------------------------------
// Internal: panic screen
// ---------------------------------------------------------------------------

static void panic(int vector, const char *exception, const char *detail)
{
    isr_increment_counter(vector);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    //vga_clear();
    vga_puts("\n*** KERNEL PANIC ***\n");
    vga_puts("Exception : ");
    vga_puts(exception);
    vga_puts("\n");
    vga_puts("Detail    : ");
    vga_puts(detail);
    vga_puts("\n\nSystem halted.\n");

    __asm__ volatile ("cli; hlt");
    for (;;) {}
}

// ---------------------------------------------------------------------------
// Exception handlers
// ISRs that receive an error code pop it from the stack before returning.
// Since we halt, the pop doesn't matter — but it's documented for correctness.
// ---------------------------------------------------------------------------

void isr_div_error_handler(void)
{
    panic(0, "#DE", "Division by zero or division overflow");
}

void isr_debug_handler(void)
{
    panic(1, "#DB", "Debug exception");
}

void isr_nmi_handler(void)
{
    panic(2, "NMI", "Non-Maskable Interrupt");
}

void isr_breakpoint_handler(void)
{
    panic(3, "#BP", "Breakpoint (INT3)");
}

void isr_overflow_handler(void)
{
    panic(4, "#OF", "Overflow (INTO)");
}

void isr_bound_range_handler(void)
{
    panic(5, "#BR", "Bound Range Exceeded");
}

void isr_invalid_opcode_handler(void)
{
    panic(6, "#UD", "Invalid Opcode");
}

void isr_device_na_handler(void)
{
    panic(7, "#NM", "Device Not Available (FPU/SSE)");
}

void isr_double_fault_handler(uint64_t error_code)
{
    (void)error_code;
    panic(8, "#DF", "Double Fault — unrecoverable, system halted");
}

void isr_default_handler(void)
{
    panic(9, "???", "Unknown / reserved exception");
}

void isr_invalid_tss_handler(uint64_t error_code)
{
    (void)error_code;
    panic(10, "#TS", "Invalid TSS");
}

void isr_segment_np_handler(uint64_t error_code)
{
    (void)error_code;
    panic(11, "#NP", "Segment Not Present");
}

void isr_stack_fault_handler(uint64_t error_code)
{
    (void)error_code;
    panic(12, "#SS", "Stack-Segment Fault");
}

void isr_gpf_handler(uint64_t error_code)
{
    (void)error_code;
    panic(13, "#GP", "General Protection Fault");
}

void isr_page_fault_handler(uint64_t error_code)
{
    // CR2 holds the faulting linear address
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    (void)error_code;
    (void)cr2;
    panic(14, "#PF", "Page Fault");
}

void isr_fpu_handler(void)
{
    panic(16, "#MF", "x87 FPU Floating-Point Error");
}

void isr_alignment_handler(uint64_t error_code)
{
    (void)error_code;
    panic(17, "#AC", "Alignment Check");
}

void isr_machine_check_handler(void)
{
    panic(18, "#MC", "Machine Check");
}

void isr_simd_fpu_handler(void)
{
    panic(19, "#XM", "SIMD Floating-Point Exception");
}

void isr_virtualization_handler(void)
{
    panic(20, "#VE", "Virtualization Exception");
}

void isr_security_handler(uint64_t error_code)
{
    (void)error_code;
    panic(30, "#SX", "Security Exception");
}

void isr_scheduler_handler(void)
{
    panic(31, "#SCHED", "Scheduler fault");
}

void isr_fs_handler(void)
{
    panic(32, "#FS", "Filesystem fault");
}