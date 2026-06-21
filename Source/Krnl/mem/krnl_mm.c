// krnl_mm.c - Kernel dynamic memory manager
//
// Implements a page-level virtual memory manager with gap-finding reuse
// and a byte-level heap allocator (kmalloc/kfree) with coalescing.

#include "krnl_mm.h"
#include "vma.h"
#include "vmm.h"
#include "pmm.h"
#include "../../Drivers/console.h"

// Head of the VMA list tracking allocated virtual ranges
static vma_t *g_kernel_vma_list = NULL;
static bool   g_initialized      = false;

// Head of the byte-level heap allocator block list
typedef struct kmalloc_header {
    size_t size;                  // size of user data block in bytes
    bool is_free;                 // true if this block is free/reusable
    char padding[7];              // padding to align 'next' pointer
    struct kmalloc_header *next;  // link to next heap block
    uint64_t pad2;                // padding to make structure size 32 bytes (multiple of 16)
} kmalloc_header_t;

static kmalloc_header_t *g_heap_head = NULL;

void krnl_mm_init(void)
{
    g_initialized = true;
    k_serial_puts("[KRNL_MM] Dynamic allocator initialized.\n");
}

bool krnl_mm_is_initialized(void)
{
    return g_initialized;
}

// Find a free virtual address range of the requested size
static uint64_t krnl_mm_find_gap(size_t size)
{
    uint64_t base = 0xFFFF800010000000ULL;
    vma_t *curr = g_kernel_vma_list;

    // Scan through the sorted VMA list to find a gap
    while (curr != NULL)
    {
        if (curr->vaddr >= base && curr->vaddr - base >= size)
        {
            return base;
        }
        if (curr->vaddr_end > base)
        {
            base = curr->vaddr_end;
        }
        curr = curr->next;
    }

    // Check space after the last VMA
    if (0xFFFF8000F0000000ULL - base >= size)
    {
        return base;
    }

    return 0; // Out of virtual address space
}

// Allocate a page-aligned range of virtual pages
void *krnl_mm_alloc_pages(size_t count)
{
    if (!g_initialized || count == 0)
    {
        return NULL;
    }

    size_t size = count * PAGE_SIZE;

    // 1. Find a free virtual address range
    uint64_t virt_ptr = krnl_mm_find_gap(size);
    if (virt_ptr == 0)
    {
        k_serial_puts("KRNL_MM ERROR: Out of virtual address space!\n");
        return NULL;
    }

    // 2. Allocate physical pages
    void *phys_ptr = pmm_alloc_pages(count);
    if (!phys_ptr)
    {
        k_serial_puts("KRNL_MM ERROR: Out of physical memory pages!\n");
        return NULL;
    }

    // 3. Register virtual address range in VMA list
    vma_insert(&g_kernel_vma_list, virt_ptr, size, 0);

    // 4. Map pages and zero-initialize them
    for (size_t i = 0; i < count; i++)
    {
        uint64_t phys = (uint64_t)phys_ptr + (i * PAGE_SIZE);
        uint64_t virt = virt_ptr + (i * PAGE_SIZE);

        if (!vmm_map_page(vmm_get_kernel_pml4(), phys, virt, PTE_PRESENT | PTE_RW))
        {
            k_serial_puts("KRNL_MM ERROR: VMM Map failed, rolling back allocation...\n");

            // Rollback already mapped pages
            for (size_t j = 0; j < i; j++)
            {
                vmm_unmap(vmm_get_kernel_pml4(), virt_ptr + (j * PAGE_SIZE));
            }

            // Free physical memory
            pmm_free_pages(phys_ptr, count);

            // Remove VMA
            vma_t *vma = vma_find(&g_kernel_vma_list, virt_ptr);
            if (vma)
            {
                vma_remove(&g_kernel_vma_list, vma);
            }

            return NULL;
        }

        // Zero out the page to prevent leaking old data
        uint64_t *word_ptr = (uint64_t *)virt;
        for (size_t k = 0; k < PAGE_SIZE / 8; k++)
        {
            word_ptr[k] = 0;
        }
    }

    return (void *)virt_ptr;
}

void *krnl_mm_alloc_page(void)
{
    return krnl_mm_alloc_pages(1);
}

// Convert virtual address to physical address
uint64_t krnl_mm_page_to_phys(void *page)
{
    pt_entry_t *pte = NULL;
    vmm_walk_ptes(vmm_get_kernel_pml4(), (uint64_t)page, 0, NULL, NULL, NULL, &pte);
    if (pte && (*pte & PTE_PRESENT))
    {
        return (*pte & PAGE_FRAME_MASK) | ((uint64_t)page & PAGE_MASK);
    }
    return 0;
}

// Free page range
void krnl_mm_free_pages(void *page, size_t count)
{
    if (!page || count == 0)
    {
        return;
    }

    uint64_t vaddr = (uint64_t)page;

    // 1. Verify that this address range is tracked by a VMA
    vma_t *vma = vma_find(&g_kernel_vma_list, vaddr);
    if (!vma)
    {
        k_serial_puts("KRNL_MM ERROR: Attempt to free invalid or unallocated virtual address!\n");
        return;
    }

    // 2. Free physical backing pages and unmap virtual pages
    for (size_t i = 0; i < count; i++)
    {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        uint64_t phys = krnl_mm_page_to_phys((void *)current_vaddr);

        if (phys)
        {
            pmm_free_page((void *)phys);
        }
        vmm_unmap(vmm_get_kernel_pml4(), current_vaddr);
    }

    // 3. Remove the VMA from the tracking list
    vma_remove(&g_kernel_vma_list, vma);
}

void krnl_mm_free_page(void *page)
{
    krnl_mm_free_pages(page, 1);
}

// kmalloc - Byte-aligned dynamic allocation
void *kmalloc(size_t size)
{
    if (size == 0)
    {
        return NULL;
    }

    // Align request size to 16 bytes for proper memory alignment
    size = (size + 15) & ~15ULL;

    // 1. Scan existing block list for a free block that fits
    kmalloc_header_t *curr = g_heap_head;
    while (curr != NULL)
    {
        if (curr->is_free && curr->size >= size)
        {
            // If the block is significantly larger, split it
            if (curr->size >= size + sizeof(kmalloc_header_t) + 16)
            {
                kmalloc_header_t *new_block = (kmalloc_header_t *)((char *)curr + sizeof(kmalloc_header_t) + size);
                new_block->size    = curr->size - size - sizeof(kmalloc_header_t);
                new_block->is_free = true;
                new_block->next    = curr->next;

                curr->size = size;
                curr->next = new_block;
            }
            curr->is_free = false;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    // 2. No suitable block found, allocate new pages from the page allocator
    size_t total_needed = size + sizeof(kmalloc_header_t);
    size_t pages_count  = (total_needed + PAGE_SIZE - 1) / PAGE_SIZE;

    kmalloc_header_t *new_block = (kmalloc_header_t *)krnl_mm_alloc_pages(pages_count);
    if (!new_block)
    {
        return NULL;
    }

    new_block->size    = (pages_count * PAGE_SIZE) - sizeof(kmalloc_header_t);
    new_block->is_free = false;
    new_block->next    = NULL;

    // Split the newly allocated block if there's excess space
    if (new_block->size >= size + sizeof(kmalloc_header_t) + 16)
    {
        kmalloc_header_t *split_block = (kmalloc_header_t *)((char *)new_block + sizeof(kmalloc_header_t) + size);
        split_block->size    = new_block->size - size - sizeof(kmalloc_header_t);
        split_block->is_free = true;
        split_block->next    = NULL;

        new_block->size = size;
        new_block->next = split_block;
    }

    // Append to list
    if (g_heap_head == NULL)
    {
        g_heap_head = new_block;
    }
    else
    {
        kmalloc_header_t *temp = g_heap_head;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = new_block;
    }

    return (void *)(new_block + 1);
}

// kfree - Free a byte-aligned allocation
void kfree(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    // Get block header
    kmalloc_header_t *header = (kmalloc_header_t *)ptr - 1;
    header->is_free = true;

    // Coalesce adjacent free blocks that are contiguous in virtual memory
    kmalloc_header_t *curr = g_heap_head;
    while (curr != NULL)
    {
        while (curr->is_free && curr->next != NULL && curr->next->is_free)
        {
            // Verify they are contiguous in memory
            uint64_t end_of_curr = (uint64_t)curr + sizeof(kmalloc_header_t) + curr->size;
            if (end_of_curr == (uint64_t)curr->next)
            {
                curr->size += sizeof(kmalloc_header_t) + curr->next->size;
                curr->next = curr->next->next;
            }
            else
            {
                break;
            }
        }
        curr = curr->next;
    }
}