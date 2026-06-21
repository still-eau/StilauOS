// vmm.h - Virtual Memory Manager
//
// Manages x86_64 4-level paging structures (PML4, PDPT, PD, PT).

#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Page table entry flags (PTE_*)
#define PTE_PRESENT       (1ULL << 0)
#define PTE_RW            (1ULL << 1)
#define PTE_USER          (1ULL << 2)
#define PTE_WRITE_THROUGH (1ULL << 3)
#define PTE_CACHE_DISABLE (1ULL << 4)
#define PTE_ACCESSED      (1ULL << 5)
#define PTE_DIRTY         (1ULL << 6)
#define PTE_GLOBAL        (1ULL << 8)
#define PTE_NX            (1ULL << 63)

// Page mapping flags for vmm_protect()
#define VMM_READ          (1ULL << 0)
#define VMM_WRITE         (1ULL << 1)
#define VMM_EXEC          (1ULL << 2)

#define PAGE_SHIFT        12
#define PAGE_SIZE         (1ULL << PAGE_SHIFT)
#define PAGE_MASK         (PAGE_SIZE - 1)

// 2 MiB page options
#define PDE_PS            (1ULL << 7)   // Page size bit for 2MiB pages
#define PAGE_SHIFT_2M     21
#define PAGE_SIZE_2M      (1ULL << PAGE_SHIFT_2M)
#define PAGE_MASK_2M      (PAGE_SIZE_2M - 1)

// Mask to extract page frame physical address (bits 12-51 in standard x86_64)
#define PAGE_FRAME_MASK   0x000FFFFFFFFFF000ULL

// PML4: Page Map Level 4
typedef volatile uint64_t pml4_entry_t;
typedef pml4_entry_t pml4_table_t[512];

// PDPT: Page Directory Pointer Table
typedef volatile uint64_t pdpt_entry_t;
typedef pdpt_entry_t pdpt_table_t[512];

// PD: Page Directory
typedef volatile uint64_t pd_entry_t;
typedef pd_entry_t pd_table_t[512];

// PT: Page Table
typedef volatile uint64_t pt_entry_t;
typedef pt_entry_t pt_table_t[512];

// Physical mapping range descriptor
typedef struct
{
    uint64_t physical_base;
    uint64_t size;          // In bytes, page aligned
    uint64_t flags;
} physical_mapping_t;

// Public API
void vmm_init(void);

// Returns the kernel's active PML4 table
pml4_table_t *vmm_get_kernel_pml4(void);

// Map a physical page to a virtual address with specified PTE flags
pt_entry_t *vmm_map_page(pml4_table_t *pml4,
                          uint64_t      phys_addr,
                          uint64_t      virt_addr,
                          uint64_t      flags);

// Unmap a virtual page
void vmm_unmap(pml4_table_t *pml4, uint64_t vaddr);

// Check if virtual address is currently present/mapped
bool vmm_is_mapped(pml4_table_t *pml4, uint64_t vaddr);

// Change flags (read, write, execute) of mapped page
int vmm_protect(pml4_table_t *pml4, uint64_t vaddr, uint64_t flags);

// Page table walk helper
uint64_t vmm_walk_ptes(pml4_table_t *pml4, uint64_t vaddr, uint64_t flags,
                       pml4_entry_t **pml4_slot, pdpt_entry_t **pdpt_slot,
                       pd_entry_t **pd_slot, pt_entry_t **pt_slot);

// Display virtual memory information
void vmm_print_info(void);

#endif // VMM_H