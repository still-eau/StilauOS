// cpu.c - CPU subsystem initialization

#include "cpu.h"
#include "gdt.h"
#include "idt.h"
#include "../../Drivers/console.h"

void cpu_init(void)
{
    kconsole_puts("\nDebug: cpu_init started\n");
    // 1. Set up the Global Descriptor Table (flat 64-bit segments)
    gdt_init();
    kconsole_puts("Debug: gdt_init done\n");

    // 2. Set up the Interrupt Descriptor Table (all 256 gates)
    //    idt_init() also calls sti internally after lidt
    idt_init();
    kconsole_puts("Debug: idt_init done\n");
}
