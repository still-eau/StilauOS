// idt.h - Interrupt Descriptor Table for x86_64

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// IDTR pointer structure
// ---------------------------------------------------------------------------

typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

// ---------------------------------------------------------------------------
// 64-bit IDT Gate Descriptor (16 bytes)
// ---------------------------------------------------------------------------

typedef struct
{
    uint16_t offset_low;         // bits  0-15  of handler address
    uint16_t segment_selector;   // kernel code segment (0x08)
    uint8_t  ist;                // Interrupt Stack Table index (0 = legacy)
    uint8_t  attributes;         // gate type, DPL, present bit
    uint16_t offset_middle;      // bits 16-31  of handler address
    uint32_t offset_high;        // bits 32-63  of handler address
    uint32_t zero;               // reserved, must be zero
} __attribute__((packed)) idt_entry_t;

// ---------------------------------------------------------------------------
// Gate attribute constants
// ---------------------------------------------------------------------------

#define IDT_GATE_INTERRUPT  0x8E    // P=1, DPL=0, type=0xE (64-bit int gate)
#define IDT_GATE_TRAP       0x8F    // P=1, DPL=0, type=0xF (64-bit trap gate)
#define IDT_GATE_USER_INT   0xEE    // P=1, DPL=3, type=0xE (user int gate)

// Kernel code segment selector (must match GDT entry [1])
#define IDT_KERNEL_CS       0x08

// ---------------------------------------------------------------------------
// CPU Exception ISR declarations (implemented in isr.c)
// ---------------------------------------------------------------------------

extern void isr_div_error(void);
extern void isr_debug(void);
extern void isr_nmi(void);
extern void isr_breakpoint(void);
extern void isr_overflow(void);
extern void isr_bound_range(void);
extern void isr_invalid_opcode(void);
extern void isr_device_na(void);
extern void isr_double_fault(void);
extern void isr_default(void);
extern void isr_invalid_tss(void);
extern void isr_segment_np(void);
extern void isr_stack_fault(void);
extern void isr_gpf(void);
extern void isr_page_fault(void);
extern void isr_fpu(void);
extern void isr_alignment(void);
extern void isr_machine_check(void);
extern void isr_simd_fpu(void);
extern void isr_virtualization(void);
extern void isr_security(void);
extern void isr_fs(void);
extern void isr_scheduler(void);

// IRQ stubs (installed by irq_install in pic.c)
extern void irq0(void);   // PIT timer
extern void irq1(void);   // keyboard
extern void irq12(void);  // PS/2 mouse
extern void isr_yield(void); // INT 0x81 cooperative yield

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void idt_init(void);
void idt_flush(void);
void idt_set_entry(int index, void (*handler)(void),
                   uint16_t segment_selector, uint8_t attributes);

#endif // IDT_H