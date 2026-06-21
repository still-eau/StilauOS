// pic.c - Intel 8259 PIC initialization and management
//
// The IBM PC/AT has two cascaded 8259 PIC chips:
//   Master PIC: IRQ 0-7   (originally vectors 8-15 — conflicts with CPU exceptions!)
//   Slave  PIC: IRQ 8-15  (originally vectors 70-77)
//
// We remap them to vectors 32-39 (Master) and 40-47 (Slave) to avoid conflicts.

#include "pic.h"
#include "io.h"
#include "../Krnl/cpu/idt.h"

// ---------------------------------------------------------------------------
// pic_init — remap both PICs and mask all IRQs by default
// ---------------------------------------------------------------------------

void pic_init(void)
{
    // Save current masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Start initialization sequence (cascade mode)
    outb(PIC1_CMD,  0x11);  io_wait();  // ICW1: init + ICW4 needed
    outb(PIC2_CMD,  0x11);  io_wait();

    // ICW2: vector offsets
    outb(PIC1_DATA, PIC_IRQ_OFFSET_MASTER); io_wait();  // Master: IRQ0 → INT 32
    outb(PIC2_DATA, PIC_IRQ_OFFSET_SLAVE);  io_wait();  // Slave:  IRQ8 → INT 40

    // ICW3: cascade wiring
    outb(PIC1_DATA, 0x04); io_wait();   // Master: slave on IRQ2 (bit 2 = 1)
    outb(PIC2_DATA, 0x02); io_wait();   // Slave:  cascade identity = 2

    // ICW4: 8086 mode
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    // Restore saved masks (mask all by default until drivers install handlers)
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    // Mask ALL IRQs until drivers explicitly unmask them
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

// ---------------------------------------------------------------------------
// pic_send_eoi — acknowledge end of interrupt
// ---------------------------------------------------------------------------

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
    {
        // For IRQs 8-15 (Slave), send EOI to slave first, then master
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

// ---------------------------------------------------------------------------
// pic_mask_irq / pic_unmask_irq
// ---------------------------------------------------------------------------

void pic_mask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t  value;

    if (irq < 8)
    {
        port  = PIC1_DATA;
    }
    else
    {
        port  = PIC2_DATA;
        irq  -= 8;
    }

    value = inb(port) | (uint8_t)(1 << irq);
    outb(port, value);
}

void pic_unmask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t  value;

    if (irq < 8)
    {
        port  = PIC1_DATA;
    }
    else
    {
        // Unmask cascade line (IRQ 2) on master PIC
        pic_unmask_irq(2);
        port  = PIC2_DATA;
        irq  -= 8;
    }

    value = inb(port) & (uint8_t)~(1 << irq);
    outb(port, value);
}

// ---------------------------------------------------------------------------
// irq_install — register an IRQ handler in the IDT and unmask the line
// ---------------------------------------------------------------------------

void irq_install(uint8_t irq, void (*handler)(void))
{
    // Vector = IRQ offset + irq number
    uint8_t vector = (irq < 8)
                     ? (uint8_t)(PIC_IRQ_OFFSET_MASTER + irq)
                     : (uint8_t)(PIC_IRQ_OFFSET_SLAVE  + (irq - 8));

    idt_set_entry(vector, handler, 0x08, IDT_GATE_INTERRUPT);
    pic_unmask_irq(irq);
}
