// gdt.h - Global Descriptor Table for x86_64

#ifndef GDT_H
#define GDT_H

#include <stddef.h>
#include <stdint.h>

// GDT structure
typedef struct
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__ ((packed)) gdt_entry_t;

typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__ ((packed)) gdt_ptr_t;

// GDT entries
static gdt_entry_t gdt[3];
static gdt_ptr_t gdt_pointer;

void gdt_init();

void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);
void gdt_load();
void gdt_flush();
void gdt_reload();
void gdt_flush_cs(uint16_t cs);
void gdt_flush_data_seg(uint16_t selector);
void gdt_flush_stack_seg(uint16_t selector);

// GDT selectors
#define GDT_NULL 0x00
#define GDT_CODE 0x08
#define GDT_DATA 0x10
#define GDT_DATA_RW 0x18
#define GDT_CODE_EXEC 0x20
#define GDT_DATA_RW_EXEC 0x28

#endif