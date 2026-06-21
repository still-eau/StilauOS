// pic.h - Intel 8259 Programmable Interrupt Controller driver

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

// PIC I/O ports
#define PIC1_CMD    0x20    // Master PIC command
#define PIC1_DATA   0x21    // Master PIC data
#define PIC2_CMD    0xA0    // Slave  PIC command
#define PIC2_DATA   0xA1    // Slave  PIC data

// PIC commands
#define PIC_EOI     0x20    // End Of Interrupt

// IRQ vector offsets after remapping
// IRQ 0-7  → vectors 32-39 (Master PIC)
// IRQ 8-15 → vectors 40-47 (Slave  PIC)
#define PIC_IRQ_OFFSET_MASTER   32
#define PIC_IRQ_OFFSET_SLAVE    40

// Initialize and remap both PICs
// Must be called before enabling interrupts
void pic_init(void);

// Send End-Of-Interrupt signal to the appropriate PIC
// irq: 0-15
void pic_send_eoi(uint8_t irq);

// Mask (disable) a specific IRQ line
// irq: 0-15
void pic_mask_irq(uint8_t irq);

// Unmask (enable) a specific IRQ line
// irq: 0-15
void pic_unmask_irq(uint8_t irq);

// Install a handler for an IRQ line and unmask it
// irq: 0-15, handler: assembly stub (declared in idt.h)
void irq_install(uint8_t irq, void (*handler)(void));

#endif // PIC_H
