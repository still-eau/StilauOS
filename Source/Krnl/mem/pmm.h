// pmm.h - Physical Memory Manager
//
// Manages physical memory pages using a dynamically sized bitmap.
// Each page is 4 KB in size.
// Bit 0 = Page is FREE
// Bit 1 = Page is USED or RESERVED

#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "../boot_info.h"

#define PMM_PAGE_SIZE   4096ULL
#define PMM_PAGE_SHIFT  12

// Public API
void     pmm_init(boot_info_t *bi, uint64_t kernel_end);
void    *pmm_alloc_page(void);
void    *pmm_alloc_pages(size_t count);
void     pmm_free_page(void *phys_addr);
void     pmm_free_pages(void *phys_addr, size_t count);

uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_used_pages(void);

void     pmm_print_info(void);

#endif // PMM_H
