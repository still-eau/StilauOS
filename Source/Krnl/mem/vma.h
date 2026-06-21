// vma.h - Virtual Memory Areas
//
// Tracks allocated virtual memory regions in a sorted linked list.
// Memory for VMA structures is obtained from a static pool to prevent
// circular dependencies with the page/heap allocator.

#ifndef VMA_H
#define VMA_H

#include <stddef.h>
#include <stdint.h>
#include "vmm.h"

typedef struct vma {
    uint64_t vaddr;
    uint64_t vaddr_end;
    uint64_t vaddr_size;
    uint64_t vaddr_offset;
    uint64_t ref_count;
    struct vma *next;
} vma_t;

void vma_init(void);

// Insert a new VMA, keeping the list sorted by virtual address.
void vma_insert(vma_t **head, uint64_t vaddr, size_t size, uint64_t offset);

// Remove a VMA from the list.
void vma_remove(vma_t **head, vma_t *vma);

// Find a VMA by virtual address.
vma_t *vma_find(vma_t **head, uint64_t vaddr);

// Find a VMA by virtual address range.
vma_t *vma_find_range(vma_t **head, uint64_t vaddr, uint64_t vaddr_end);

// Find a VMA by offset.
vma_t *vma_find_offset(vma_t **head, uint64_t offset);

// Free all VMAs in the list.
void vma_free(vma_t **head);

#endif  // VMA_H