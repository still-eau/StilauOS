// vmm.c - Virtual Memory Manager
//
// Handles page table walks, page mapping, unmapping, and page protection
// for the x86_64 architecture using 4-level paging.

#include "vmm.h"
#include "pmm.h"
#include "../../Drivers/console.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Global pointer to the active kernel PML4 (loaded from CR3 at boot)
static pml4_table_t *g_kernel_pml4 = NULL;

// Flush the TLB entry for a single virtual address
static inline void invlpg(uint64_t vaddr)
{
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
}

// Zero-fill a 4 KB page table (512 entries of 8 bytes)
static void zero_page(uint64_t *table)
{
    for (int i = 0; i < 512; i++)
    {
        table[i] = 0;
    }
}

// Allocate a new page table from PMM, zero it, and return its physical address
static uint64_t alloc_table(void)
{
    uint64_t *page = (uint64_t *)pmm_alloc_page();
    if (!page)
    {
        return 0;
    }
    zero_page(page);
    return (uint64_t)page;
}

// Extract page table indices from virtual address
static inline uint64_t pml4_idx(uint64_t v) { return (v >> 39) & 0x1FF; }
static inline uint64_t pdpt_idx(uint64_t v) { return (v >> 30) & 0x1FF; }
static inline uint64_t pd_idx  (uint64_t v) { return (v >> 21) & 0x1FF; }
static inline uint64_t pt_idx  (uint64_t v) { return (v >> 12) & 0x1FF; }

// Initialize VMM by reading CR3
void vmm_init(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    
    // In identity mapping, physical pointer == virtual pointer
    g_kernel_pml4 = (pml4_table_t *)(cr3 & PAGE_FRAME_MASK);
    kprintf("[VMM] kernel PML4 at physical address 0x%x\n", (unsigned)(cr3 & PAGE_FRAME_MASK));
}

pml4_table_t *vmm_get_kernel_pml4(void)
{
    return g_kernel_pml4;
}

// Walk the 4-level page table hierarchy
// If dynamic allocation is needed and PTE_PRESENT is set in flags, new tables are allocated.
uint64_t vmm_walk_ptes(pml4_table_t *pml4,
                       uint64_t      vaddr,
                       uint64_t      flags,
                       pml4_entry_t **pml4_slot,
                       pdpt_entry_t **pdpt_slot,
                       pd_entry_t   **pd_slot,
                       pt_entry_t   **pt_slot)
{
    bool create = (flags & PTE_PRESENT) != 0;

    // PML4 Table
    pml4_entry_t *e4 = &(*pml4)[pml4_idx(vaddr)];
    if (pml4_slot) *pml4_slot = e4;

    if (!(*e4 & PTE_PRESENT))
    {
        if (!create) return 0;
        uint64_t tbl = alloc_table();
        if (!tbl) return 0;
        *e4 = tbl | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // PDPT Table
    pdpt_table_t *pdpt = (pdpt_table_t *)(*e4 & PAGE_FRAME_MASK);
    pdpt_entry_t *e3   = &(*pdpt)[pdpt_idx(vaddr)];
    if (pdpt_slot) *pdpt_slot = e3;

    if (!(*e3 & PTE_PRESENT))
    {
        if (!create) return 0;
        uint64_t tbl = alloc_table();
        if (!tbl) return 0;
        *e3 = tbl | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // PD Table
    pd_table_t *pd = (pd_table_t *)(*e3 & PAGE_FRAME_MASK);
    pd_entry_t *e2 = &(*pd)[pd_idx(vaddr)];
    if (pd_slot) *pd_slot = e2;

    if (!(*e2 & PTE_PRESENT))
    {
        if (!create) return 0;
        uint64_t tbl = alloc_table();
        if (!tbl) return 0;
        *e2 = tbl | PTE_PRESENT | PTE_RW | PTE_USER;
    }

    // Handle 2 MiB huge page if present
    if (*e2 & PDE_PS)
    {
        if (pt_slot) *pt_slot = NULL;
        return *e2 & PAGE_FRAME_MASK;
    }

    // PT Table (Leaf)
    pt_table_t *pt = (pt_table_t *)(*e2 & PAGE_FRAME_MASK);
    pt_entry_t *e1 = &(*pt)[pt_idx(vaddr)];
    if (pt_slot) *pt_slot = e1;

    return *e1 & PAGE_FRAME_MASK;
}

// Map page
pt_entry_t *vmm_map_page(pml4_table_t *pml4,
                          uint64_t      phys_addr,
                          uint64_t      virt_addr,
                          uint64_t      flags)
{
    pt_entry_t *pte = NULL;

    // Walk the page table, creating intermediate levels if present bit is set
    vmm_walk_ptes(pml4, virt_addr, PTE_PRESENT, NULL, NULL, NULL, &pte);

    if (!pte)
    {
        return NULL;
    }

    // Set page descriptor
    *pte = (phys_addr & PAGE_FRAME_MASK) | flags;

    // Flush TLB
    invlpg(virt_addr);

    return pte;
}

// Unmap page
void vmm_unmap(pml4_table_t *pml4, uint64_t vaddr)
{
    pt_entry_t *pte = NULL;

    vmm_walk_ptes(pml4, vaddr, 0, NULL, NULL, NULL, &pte);

    if (pte && (*pte & PTE_PRESENT))
    {
        *pte = 0;
        invlpg(vaddr);
    }
}

// Check if mapped
bool vmm_is_mapped(pml4_table_t *pml4, uint64_t vaddr)
{
    pt_entry_t *pte = NULL;

    vmm_walk_ptes(pml4, vaddr, 0, NULL, NULL, NULL, &pte);

    return (pte && (*pte & PTE_PRESENT) != 0);
}

// Change protection flags
int vmm_protect(pml4_table_t *pml4, uint64_t vaddr, uint64_t flags)
{
    pt_entry_t *pte = NULL;

    vmm_walk_ptes(pml4, vaddr, 0, NULL, NULL, NULL, &pte);

    if (!pte || !(*pte & PTE_PRESENT))
    {
        return -1;
    }

    // Retain physical frame and accessed/dirty/global flags
    uint64_t entry = *pte & (PAGE_FRAME_MASK | PTE_ACCESSED | PTE_DIRTY | PTE_GLOBAL);

    entry |= PTE_PRESENT;

    if (flags & VMM_WRITE)
    {
        entry |= PTE_RW;
    }
    else
    {
        entry &= ~PTE_RW;
    }

    if (flags & VMM_EXEC)
    {
        entry &= ~PTE_NX;
    }
    else
    {
        entry |= PTE_NX;
    }

    *pte = entry;
    invlpg(vaddr);

    return 0;
}

// Print statistics
void vmm_print_info(void)
{
    if (!g_kernel_pml4)
    {
        kprintln("VMM not initialized.");
        return;
    }

    uint64_t pml4_count = 0;
    uint64_t pdpt_count = 0;
    uint64_t pd_count   = 0;
    uint64_t pt_count   = 0;
    uint64_t huge_2m    = 0;
    uint64_t pages_4k   = 0;

    for (int i = 0; i < 512; i++)
    {
        pml4_entry_t pml4e = (*g_kernel_pml4)[i];
        if (!(pml4e & PTE_PRESENT)) continue;

        pml4_count++;

        pdpt_table_t *pdpt = (pdpt_table_t *)(pml4e & PAGE_FRAME_MASK);

        for (int j = 0; j < 512; j++)
        {
            pdpt_entry_t pdpte = (*pdpt)[j];
            if (!(pdpte & PTE_PRESENT)) continue;

            pdpt_count++;

            pd_table_t *pd = (pd_table_t *)(pdpte & PAGE_FRAME_MASK);

            for (int k = 0; k < 512; k++)
            {
                pd_entry_t pde = (*pd)[k];
                if (!(pde & PTE_PRESENT)) continue;

                pd_count++;

                if (pde & PDE_PS)
                {
                    huge_2m++;
                    continue;
                }

                pt_count++;

                pt_table_t *pt = (pt_table_t *)(pde & PAGE_FRAME_MASK);

                for (int l = 0; l < 512; l++)
                {
                    if ((*pt)[l] & PTE_PRESENT)
                    {
                        pages_4k++;
                    }
                }
            }
        }
    }

    uint64_t mapped_bytes = huge_2m * (2ULL * 1024 * 1024) + pages_4k * 4096ULL;

    kprintln("Virtual Memory (VMM)");
    kprintln("--------------------");

    kprintf("PML4 entries : %d\n", (int)pml4_count);
    kprintf("PDPT entries : %d\n", (int)pdpt_count);
    kprintf("PD entries   : %d\n", (int)pd_count);
    kprintf("PT tables    : %d\n", (int)pt_count);
    kprintf("2 MiB pages  : %d\n", (int)huge_2m);
    kprintf("4 KiB pages  : %d\n", (int)pages_4k);
    kprintf("Mapped memory: %d MiB\n", (int)(mapped_bytes / 1024 / 1024));
}