// vma.c - Virtual Memory Areas
//
// Tracks allocated virtual memory regions using a sorted list.
// To avoid infinite recursion during early boot or page allocation,
// VMA structures are allocated from a pre-allocated static pool.

#include "vma.h"
#include "../../Drivers/console.h"

#define VMA_POOL_SIZE 1024

// Statically allocated pool for VMAs to avoid circular dependencies
static vma_t g_vma_pool[VMA_POOL_SIZE];
static bool  g_vma_used[VMA_POOL_SIZE];

// Local helper to initialize memory region
static void *vma_memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

// Allocate a node from the static pool
static vma_t *vma_allocate_node(void)
{
    for (size_t i = 0; i < VMA_POOL_SIZE; i++)
    {
        if (!g_vma_used[i])
        {
            g_vma_used[i] = true;
            return &g_vma_pool[i];
        }
    }
    return NULL;
}

// Return a node to the pool
static void vma_free_node(vma_t *vma)
{
    if (vma >= g_vma_pool && vma < &g_vma_pool[VMA_POOL_SIZE])
    {
        size_t index = vma - g_vma_pool;
        g_vma_used[index] = false;
    }
}

// Initialize VMA subsystem
void vma_init(void)
{
    vma_memset(g_vma_pool, 0, sizeof(g_vma_pool));
    vma_memset(g_vma_used, 0, sizeof(g_vma_used));
}

// Insert a VMA, keeping the list sorted by virtual address (ascending)
void vma_insert(vma_t **head, uint64_t vaddr, size_t size, uint64_t offset)
{
    vma_t *new_vma = vma_allocate_node();
    if (!new_vma)
    {
        k_serial_puts("VMA ERROR: Static pool exhausted, cannot create VMA!\n");
        return;
    }

    vma_memset(new_vma, 0, sizeof(vma_t));
    new_vma->vaddr        = vaddr;
    new_vma->vaddr_size   = size;
    new_vma->vaddr_end    = vaddr + size;
    new_vma->vaddr_offset = offset;
    new_vma->ref_count    = 1;
    new_vma->next         = NULL;

    // If list is empty or new VMA has lower address than head
    if (*head == NULL || vaddr < (*head)->vaddr)
    {
        new_vma->next = *head;
        *head = new_vma;
        return;
    }

    // Traverse to find the insertion point
    vma_t *curr = *head;
    while (curr->next != NULL && curr->next->vaddr < vaddr)
    {
        curr = curr->next;
    }

    new_vma->next = curr->next;
    curr->next = new_vma;
}

// Remove a VMA from the list and return it to the pool
void vma_remove(vma_t **head, vma_t *vma)
{
    if (!*head || !vma)
    {
        return;
    }

    if (*head == vma)
    {
        *head = vma->next;
        vma_free_node(vma);
        return;
    }

    vma_t *curr = *head;
    while (curr->next && curr->next != vma)
    {
        curr = curr->next;
    }

    if (curr->next == vma)
    {
        curr->next = vma->next;
        vma_free_node(vma);
    }
}

// Find a VMA by virtual address
vma_t *vma_find(vma_t **head, uint64_t vaddr)
{
    vma_t *curr = *head;
    while (curr)
    {
        if (vaddr >= curr->vaddr && vaddr < curr->vaddr_end)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Find a VMA by virtual address range (overlap check)
vma_t *vma_find_range(vma_t **head, uint64_t vaddr, uint64_t vaddr_end)
{
    vma_t *curr = *head;
    while (curr)
    {
        if (vaddr < curr->vaddr_end && vaddr_end > curr->vaddr)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Find a VMA by offset
vma_t *vma_find_offset(vma_t **head, uint64_t offset)
{
    vma_t *curr = *head;
    while (curr)
    {
        if (curr->vaddr_offset == offset)
        {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Free all VMAs in the list
void vma_free(vma_t **head)
{
    vma_t *curr = *head;
    while (curr)
    {
        vma_t *next = curr->next;
        vma_free_node(curr);
        curr = next;
    }
    *head = NULL;
}