// idt.c - Interrupt Descriptor Table for x86_64
//
// Sets up all 256 IDT entries:
//   [0 - 31]  CPU exceptions (Division by zero, Page fault, etc.)
//   [32-47]   Hardware IRQs remapped via PIC (after pic_init)
//   [48-255]  Reserved / available for software interrupts

#include "idt.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Static IDT storage
// ---------------------------------------------------------------------------

static idt_entry_t idt[256];
static idt_ptr_t   idt_pointer;

// ---------------------------------------------------------------------------
// Exception handler stubs — defined in idt_stubs.asm (or inline asm below)
// We declare them extern so the linker can find them.
// ---------------------------------------------------------------------------

// Generic exception stub that halts — replaced per-vector for real handling
static void idt_default_handler(void)
{
    __asm__ volatile ("cli; hlt");
}

// ---------------------------------------------------------------------------
// idt_encode_entry — fill one IDT gate descriptor
//
// For a 64-bit Interrupt Gate:
//   attributes = 0x8E  (P=1, DPL=0, type=0xE = 64-bit interrupt gate)
// For a Trap Gate:
//   attributes = 0x8F  (P=1, DPL=0, type=0xF = 64-bit trap gate)
// ---------------------------------------------------------------------------

static void idt_encode_entry(int index,
                              void    (*handler)(void),
                              uint16_t segment_selector,
                              uint8_t  attributes)
{
    uint64_t offset = (uint64_t)(uintptr_t)handler;

    idt_entry_t *e = &idt[index];

    e->offset_low      = (uint16_t)(offset & 0xFFFF);
    e->offset_middle   = (uint16_t)((offset >> 16) & 0xFFFF);
    e->offset_high     = (uint32_t)((offset >> 32) & 0xFFFFFFFF);
    e->segment_selector = segment_selector;
    e->ist             = 0;        // IST = 0: use legacy stack switching
    e->attributes      = attributes;
    e->zero            = 0;
}

// ---------------------------------------------------------------------------
// Public: idt_set_entry
// ---------------------------------------------------------------------------

void idt_set_entry(int index, void (*handler)(void),
                   uint16_t segment_selector, uint8_t attributes)
{
    idt_encode_entry(index, handler, segment_selector, attributes);
}

// ---------------------------------------------------------------------------
// idt_flush — load the IDTR register
// ---------------------------------------------------------------------------

void idt_flush(void)
{
    __asm__ volatile (
        "lidt (%0)\n\t"
        :
        : "r" (&idt_pointer)
        : "memory"
    );
}

// ---------------------------------------------------------------------------
// idt_init — configure all 256 entries and activate the IDT
// ---------------------------------------------------------------------------

void idt_init(void)
{
    // Build the IDTR
    idt_pointer.limit = (uint16_t)(sizeof(idt) - 1);
    idt_pointer.base  = (uint64_t)(uintptr_t)idt;

    // Fill every entry with the default (halt) handler first
    for (int i = 0; i < 256; i++)
    {
        idt_encode_entry(i,
                         idt_default_handler,
                         IDT_KERNEL_CS,          // kernel code selector 0x08
                         IDT_GATE_INTERRUPT);    // 64-bit interrupt gate
    }

    // CPU exceptions (0-31) — trap gates so flags are preserved, useful for
    // debuggers. Switch to interrupt gate (IDT_GATE_INTERRUPT) if you want
    // interrupts disabled while handling.

    // [0]  #DE  Division Error
    idt_encode_entry( 0, isr_div_error,          IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [1]  #DB  Debug
    idt_encode_entry( 1, isr_debug,              IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [2]  NMI  Non-Maskable Interrupt
    idt_encode_entry( 2, isr_nmi,                IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [3]  #BP  Breakpoint
    idt_encode_entry( 3, isr_breakpoint,         IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [4]  #OF  Overflow
    idt_encode_entry( 4, isr_overflow,           IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [5]  #BR  Bound Range Exceeded
    idt_encode_entry( 5, isr_bound_range,        IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [6]  #UD  Invalid Opcode
    idt_encode_entry( 6, isr_invalid_opcode,     IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [7]  #NM  Device Not Available
    idt_encode_entry( 7, isr_device_na,          IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [8]  #DF  Double Fault  (pushes error code = 0, always)
    idt_encode_entry( 8, isr_double_fault,       IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [9]  Coprocessor Segment Overrun (legacy, not generated on modern CPUs)
    idt_encode_entry( 9, isr_default,            IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [10] #TS  Invalid TSS
    idt_encode_entry(10, isr_invalid_tss,        IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [11] #NP  Segment Not Present
    idt_encode_entry(11, isr_segment_np,         IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [12] #SS  Stack-Segment Fault
    idt_encode_entry(12, isr_stack_fault,        IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [13] #GP  General Protection Fault
    idt_encode_entry(13, isr_gpf,                IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [14] #PF  Page Fault
    idt_encode_entry(14, isr_page_fault,         IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [15] Reserved
    // [16] #MF  x87 Floating-Point Exception
    idt_encode_entry(16, isr_fpu,                IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [17] #AC  Alignment Check
    idt_encode_entry(17, isr_alignment,          IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [18] #MC  Machine Check
    idt_encode_entry(18, isr_machine_check,      IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [19] #XM  SIMD Floating-Point Exception
    idt_encode_entry(19, isr_simd_fpu,           IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [20] #VE  Virtualization Exception
    idt_encode_entry(20, isr_virtualization,     IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [21-29] Reserved
    // [30] #SX  Security Exception
    idt_encode_entry(30, isr_security,           IDT_KERNEL_CS, IDT_GATE_TRAP);
    // [31] #FS  File System Fault
    idt_encode_entry(31, isr_fs, IDT_KERNEL_CS, IDT_GATE_INTERRUPT);
    // [32] #SCHED  Scheduler Fault
    idt_encode_entry(32, isr_scheduler, IDT_KERNEL_CS, IDT_GATE_INTERRUPT);

    // Hardware IRQs [32-47] are installed by pic_init() + irq_install()
    // (see drivers/pic.c). We leave the default handler for now.

    // [0x81] Cooperative scheduler yield — INT 0x81 fired by thread_yield().
    idt_encode_entry(0x81, isr_yield, IDT_KERNEL_CS, IDT_GATE_INTERRUPT);

    // Load the IDT — do NOT enable interrupts here!
    // sti must only be called AFTER pic_init() has remapped the PIC.
    // Without the remap, IRQ0 (PIT timer) fires on vector 8 = #DF (Double Fault).
    idt_flush();
    // NOTE: interrupts remain DISABLED until after pic_init()
}