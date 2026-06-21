// isr.h - CPU Exception Handler declarations

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

// C-level exception handlers called from assembly stubs
void isr_div_error_handler(void);
void isr_debug_handler(void);
void isr_nmi_handler(void);
void isr_breakpoint_handler(void);
void isr_overflow_handler(void);
void isr_bound_range_handler(void);
void isr_invalid_opcode_handler(void);
void isr_device_na_handler(void);
void isr_double_fault_handler(uint64_t error_code);
void isr_default_handler(void);
void isr_invalid_tss_handler(uint64_t error_code);
void isr_segment_np_handler(uint64_t error_code);
void isr_stack_fault_handler(uint64_t error_code);
void isr_gpf_handler(uint64_t error_code);
void isr_page_fault_handler(uint64_t error_code);
void isr_fpu_handler(void);
void isr_alignment_handler(uint64_t error_code);
void isr_machine_check_handler(void);
void isr_simd_fpu_handler(void);
void isr_virtualization_handler(void);
void isr_security_handler(uint64_t error_code);
void isr_scheduler_handler(void);
void isr_fs_handler(void);

// IRQ C-level handlers
void irq0_handler(void);   // PIT timer
void irq1_handler(void);   // Keyboard

// Interrupt tracking & descriptions
const char *isr_get_description(int vector);
uint64_t isr_get_count(int vector);
void isr_increment_counter(int vector);

#endif // ISR_H
