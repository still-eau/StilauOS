// gdt.c - Global Descriptor Table for x86_64
//
// In 64-bit long mode, base/limit are mostly ignored by the CPU,
// but the access byte and flags still matter (especially the L bit for CS).
// We set up three entries:
//   [0] Null descriptor  (required by the CPU spec)  -> selector 0x00
//   [1] Kernel code 64-bit                           -> selector 0x08
//   [2] Kernel data                                  -> selector 0x10

#include "gdt.h"

// ---------------------------------------------------------------------------
// Static GDT storage (3 entries + the GDTR pointer)
// Defined here (not in the header) to avoid multiple-definition issues.
// ---------------------------------------------------------------------------

static gdt_entry_t gdt[3];
static gdt_ptr_t   gdt_pointer;

// ---------------------------------------------------------------------------
// Internal helper: encode one raw GDT entry in place
// ---------------------------------------------------------------------------

static void gdt_encode_entry(int index,
                              uint32_t base,
                              uint32_t limit,
                              uint8_t  access,
                              uint8_t  flags)
{
    gdt_entry_t *e = &gdt[index];

    // Limit bits 0-15
    e->limit_low = (uint16_t)(limit & 0xFFFF);

    // Base bits 0-15, 16-23, 24-31
    e->base_low    = (uint16_t)(base & 0xFFFF);
    e->base_middle = (uint8_t)((base >> 16) & 0xFF);
    e->base_high   = (uint8_t)((base >> 24) & 0xFF);

    // Access byte
    e->access = access;

    // Granularity byte: upper nibble = flags (G, D, L, AVL),
    //                   lower nibble = limit bits 16-19
    e->granularity = (uint8_t)((flags & 0xF0) | ((limit >> 16) & 0x0F));
}

// ---------------------------------------------------------------------------
// gdt_set_entry — public wrapper so other kernel code can add entries later
// ---------------------------------------------------------------------------

void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                   uint8_t access, uint8_t flags)
{
    gdt_encode_entry(index, base, limit, access, flags);
}

// ---------------------------------------------------------------------------
// gdt_flush — load GDTR and perform a far-return into the new CS
//
// A regular jmp cannot change CS in 64-bit protected mode from C, so we
// use the classic "push selector; push rip; lretq" trick to do a far return
// that reloads CS, then reload all the data-segment registers.
// ---------------------------------------------------------------------------

void gdt_flush(void)
{
    // 1. Load the GDTR register
    __asm__ volatile (
        "lgdt (%0)\n\t"
        :
        : "r" (&gdt_pointer)
        : "memory"
    );

    // 2. Far-return to reload CS (0x08), then reload DS/ES/FS/GS/SS (0x10)
    __asm__ volatile (
        "pushq %0\n\t"                      // push new CS value
        "lea   1f(%%rip), %%rax\n\t"        // push address of label 1:
        "pushq %%rax\n\t"
        "lretq\n\t"                         // far-return: pops RIP then CS
        "1:\n\t"
        "movw  %1, %%ax\n\t"
        "movw  %%ax, %%ds\n\t"
        "movw  %%ax, %%es\n\t"
        "movw  %%ax, %%fs\n\t"
        "movw  %%ax, %%gs\n\t"
        "movw  %%ax, %%ss\n\t"
        :
        : "i" ((uint64_t)GDT_CODE),         // GDT_CODE  = 0x08 (from gdt.h)
          "i" ((uint64_t)GDT_DATA)          // GDT_DATA  = 0x10 (from gdt.h)
        : "rax", "memory"
    );
}

// ---------------------------------------------------------------------------
// gdt_init — configure the three standard segments and activate the GDT
// ---------------------------------------------------------------------------

void gdt_init(void)
{
    // Build the GDTR: limit = table size - 1, base = virtual address of gdt[]
    gdt_pointer.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_pointer.base  = (uint64_t)(uintptr_t)gdt;

    // -----------------------------------------------------------------------
    // [0] Null descriptor — all zeros, required by the CPU specification
    // -----------------------------------------------------------------------
    gdt_encode_entry(0, 0, 0, 0x00, 0x00);

    // -----------------------------------------------------------------------
    // [1] Kernel code segment — selector 0x08
    //   Access 0x9A : P=1 (present), DPL=00 (ring 0), S=1 (code/data),
    //                 Type=1010 (execute + read, non-conforming)
    //   Flags  0x20 : G=0, D=0 (MUST be 0 when L=1), L=1 (64-bit code), AVL=0
    //   In 64-bit long mode the CPU ignores base and limit for CS.
    // -----------------------------------------------------------------------
    gdt_encode_entry(1,
                     0x00000000,  // base  (ignored in long mode)
                     0xFFFFF,     // limit (ignored in long mode)
                     0x9A,        // access: present, ring0, code, exec/read
                     0x20);       // flags:  L=1 → 64-bit code segment

    // -----------------------------------------------------------------------
    // [2] Kernel data segment — selector 0x10
    //   Access 0x92 : P=1, DPL=00, S=1, Type=0010 (read/write, expand-up)
    //   Flags  0x00 : no special flags required for data in long mode
    // -----------------------------------------------------------------------
    gdt_encode_entry(2,
                     0x00000000,
                     0xFFFFF,
                     0x92,        // access: present, ring0, data, read/write
                     0x00);

    // Activate the new GDT and reload all segment registers
    gdt_flush();
}