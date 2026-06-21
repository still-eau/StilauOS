// krnl_mm.h - Kernel dynamic memory manager
//
// Manages page-aligned dynamic allocations (backed by VMA list to prevent leaks)
// and byte-aligned allocations (kmalloc/kfree) on a kernel heap.

#ifndef KRNL_MM_H
#define KRNL_MM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Page-level Allocator API
void     krnl_mm_init(void);
bool     krnl_mm_is_initialized(void);

void    *krnl_mm_alloc_page(void);
void    *krnl_mm_alloc_pages(size_t count);

void     krnl_mm_free_page(void *page);
void     krnl_mm_free_pages(void *page, size_t count);

uint64_t krnl_mm_page_to_phys(void *page);

// Byte-level Allocator API (Kernel Heap)
void    *kmalloc(size_t size);
void     kfree(void *ptr);

#endif // KRNL_MM_H