// pmm.c - Physical Memory Manager
//
// Manages physical memory pages using a dynamically sized bitmap.
// The bitmap size is calculated based on the maximum physical address
// reported in the E820 memory map. The bitmap itself is stored in a
// free physical memory region above the kernel.

#include "pmm.h"
#include "../../Drivers/console.h"
#include <stdbool.h>
#include <stddef.h>

// PMM internal state
static uint8_t  *g_pmm_bitmap     = NULL;
static uint64_t  g_pmm_max_pages  = 0;
static uint64_t  g_pmm_total_pages = 0;
static uint64_t  g_pmm_free_pages  = 0;
static uint64_t  g_pmm_alloc_hint  = 0;

// Bitmap primitives
static inline void bitmap_set(uint64_t page)
{
    g_pmm_bitmap[page >> 3] |= (uint8_t)(1u << (page & 7));
}

static inline void bitmap_clear(uint64_t page)
{
    g_pmm_bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7));
}

static inline int bitmap_test(uint64_t page)
{
    return (g_pmm_bitmap[page >> 3] >> (page & 7)) & 1;
}

// Mark a range of physical memory as USED/RESERVED
static void pmm_reserve_region(uint64_t start_phys, uint64_t end_phys)
{
    uint64_t first_page = start_phys >> PMM_PAGE_SHIFT;
    uint64_t last_page  = (end_phys + PMM_PAGE_SIZE - 1) >> PMM_PAGE_SHIFT;

    for (uint64_t p = first_page; p < last_page && p < g_pmm_max_pages; p++)
    {
        if (bitmap_test(p) == 0)
        {
            bitmap_set(p);
            if (g_pmm_free_pages > 0)
            {
                g_pmm_free_pages--;
            }
        }
    }
}

// Initialize Physical Memory Manager
void pmm_init(boot_info_t *bi, uint64_t kernel_end)
{
    // 0. VÉRIFICATION DE SÉCURITÉ CRITIQUE
    // Si la magie est invalide, le pointeur bi est corrompu ou non initialisé
    if (bi == NULL || bi->magic != BOOT_MAGIC) {
        kprintf("PMM CRITICAL ERROR: Boot info invalid! Magic: %x\n", (bi ? (uint32_t)bi->magic : 0));
        for (;;);
    }

    // 1. Détermination de la taille totale (Max adresse)
    uint64_t max_phys_addr = 0;
    for (uint64_t i = 0; i < bi->mmap_count; i++)
    {
        uint64_t entry_end = bi->mmap[i].base + bi->mmap[i].length;
        if (entry_end > max_phys_addr) max_phys_addr = entry_end;
    }

    g_pmm_max_pages = max_phys_addr >> PMM_PAGE_SHIFT;
    uint64_t bitmap_size = (g_pmm_max_pages + 7) >> 3;

    // 2. Recherche d'un emplacement pour le bitmap
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < bi->mmap_count; i++)
    {
        if (bi->mmap[i].type != MMAP_TYPE_USABLE) continue;

        uint64_t start = (bi->mmap[i].base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        uint64_t end   = (bi->mmap[i].base + bi->mmap[i].length) & ~(PMM_PAGE_SIZE - 1);

        if (start < kernel_end) start = (kernel_end + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        
        if (start + bitmap_size <= end) {
            bitmap_phys = start;
            break;
        }
    }

    if (bitmap_phys == 0) {
        kprintf("PMM ERROR: Out of memory for bitmap! Size needed: %d bytes\n", (int)bitmap_size);
        for (;;);
    }

    g_pmm_bitmap = (uint8_t *)bitmap_phys;
    
    // Initialiser bitmap à 1 (utilisé)
    for (uint64_t i = 0; i < bitmap_size; i++) g_pmm_bitmap[i] = 0xFF;

    // 3 & 4. Marquage des pages
    g_pmm_total_pages = 0;
    g_pmm_free_pages = 0;

    for (uint64_t i = 0; i < bi->mmap_count; i++)
    {
        if (bi->mmap[i].type != MMAP_TYPE_USABLE) continue;
        uint64_t start = (bi->mmap[i].base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        uint64_t end   = (bi->mmap[i].base + bi->mmap[i].length) & ~(PMM_PAGE_SIZE - 1);

        for (uint64_t p = (start >> PMM_PAGE_SHIFT); p < (end >> PMM_PAGE_SHIFT); p++) {
            if (p < g_pmm_max_pages) {
                bitmap_clear(p);
                g_pmm_free_pages++;
                g_pmm_total_pages++;
            }
        }
    }

    // 5. Réservation des zones critiques
    pmm_reserve_region(0, 0x100000); // BIOS/Legacy
    pmm_reserve_region(0x100000, kernel_end); // Kernel
    pmm_reserve_region(bitmap_phys, bitmap_phys + bitmap_size); // Bitmap itself
}

// Allocate a single page
void *pmm_alloc_page(void)
{
    return pmm_alloc_pages(1);
}

// Allocate contiguous pages
void *pmm_alloc_pages(size_t count)
{
    if (count == 0 || g_pmm_free_pages < count)
    {
        return NULL;
    }

    for (uint64_t offset = 0; offset < g_pmm_max_pages; offset++)
    {
        uint64_t start_page = (g_pmm_alloc_hint + offset) % g_pmm_max_pages;

        // Ensure we do not wrap around to index 0 during a contiguous block scan
        if (start_page + count > g_pmm_max_pages)
        {
            continue;
        }

        bool possible = true;
        for (size_t i = 0; i < count; i++)
        {
            if (bitmap_test(start_page + i) != 0)
            {
                possible = false;
                break;
            }
        }

        if (possible)
        {
            for (size_t i = 0; i < count; i++)
            {
                bitmap_set(start_page + i);
            }
            g_pmm_free_pages -= count;
            g_pmm_alloc_hint = (start_page + count) % g_pmm_max_pages;
            return (void *)(start_page << PMM_PAGE_SHIFT);
        }
    }

    return NULL;
}

// Free a single page
void pmm_free_page(void *phys_addr)
{
    pmm_free_pages(phys_addr, 1);
}

// Free contiguous pages
void pmm_free_pages(void *phys_addr, size_t count)
{
    if (!phys_addr || count == 0)
    {
        return;
    }

    uint64_t start_page = (uint64_t)phys_addr >> PMM_PAGE_SHIFT;

    if (start_page >= g_pmm_max_pages || start_page + count > g_pmm_max_pages)
    {
        return;
    }

    for (size_t i = 0; i < count; i++)
    {
        uint64_t p = start_page + i;
        if (bitmap_test(p) != 0)
        {
            bitmap_clear(p);
            g_pmm_free_pages++;
        }
    }

    if (start_page < g_pmm_alloc_hint)
    {
        g_pmm_alloc_hint = start_page;
    }
}

uint64_t pmm_get_total_pages(void)
{
    return g_pmm_total_pages;
}

uint64_t pmm_get_free_pages(void)
{
    return g_pmm_free_pages;
}

uint64_t pmm_get_used_pages(void)
{
    return g_pmm_total_pages - g_pmm_free_pages;
}

void pmm_print_info(void)
{
    uint64_t total_kb = g_pmm_total_pages * (PMM_PAGE_SIZE / 1024);
    uint64_t free_kb  = g_pmm_free_pages * (PMM_PAGE_SIZE / 1024);
    uint64_t used_kb  = (g_pmm_total_pages - g_pmm_free_pages) * (PMM_PAGE_SIZE / 1024);

    kprintf("  Total usable : %d KB  (%d pages)\n", (int)total_kb, (int)g_pmm_total_pages);
    kprintf("  Used/reserved: %d KB  (%d pages)\n", (int)used_kb,  (int)pmm_get_used_pages());
    kprintf("  Free         : %d KB  (%d pages)\n", (int)free_kb,  (int)g_pmm_free_pages);
}
