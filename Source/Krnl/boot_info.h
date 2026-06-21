// boot_info.h - Boot information structure passed from bootloader to kernel
//
// Must match the exact layout declared in boot_entry.asm (.data section).
// All fields are 8-byte aligned (dq in NASM).

#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Magic number
// ---------------------------------------------------------------------------

#define BOOT_MAGIC  0xB007B007UL

// ---------------------------------------------------------------------------
// E820 memory map entry
// ---------------------------------------------------------------------------

typedef struct
{
    uint64_t base;      // physical start address
    uint64_t length;    // region length in bytes
    uint64_t type;      // 1 = usable RAM, other = reserved/ACPI/etc.
} __attribute__((packed)) boot_mmap_entry_t;

// E820 type values
#define MMAP_TYPE_USABLE    1
#define MMAP_TYPE_RESERVED  2
#define MMAP_TYPE_ACPI_RCLM 3
#define MMAP_TYPE_ACPI_NVS  4
#define MMAP_TYPE_BAD       5

// ---------------------------------------------------------------------------
// Boot information block
// ---------------------------------------------------------------------------

typedef struct
{
    uint64_t magic;           // must equal BOOT_MAGIC
    uint64_t mem_lower;       // available lower memory (bytes, < 1 MB)
    uint64_t mem_upper;       // available upper memory (bytes, > 1 MB)
    uint64_t pml4_addr;       // physical address of PML4 table
    uint64_t vbe_mode_info;   // VBE mode info block address (0 if none)
    uint64_t cmdline;         // kernel command line address (0 if none)
    uint64_t mmap_count;      // number of entries in mmap[]
    boot_mmap_entry_t mmap[]; // flexible array of E820 entries
} __attribute__((packed)) boot_info_t;

#endif // BOOT_INFO_H
