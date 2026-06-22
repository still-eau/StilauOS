// alloc.h
//
// StilauFS allocation header

#ifndef STILAUFS_ALLOC_H
#define STILAUFS_ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../fs_types.h"

#define STILAUFS_BITMAP_BITS   4096
#define STILAUFS_BITMAP_BYTES  512

#define STILAUFS_MAX_INODES    128

// General bitwise allocation helper
int stilaufs_alloc_bit(uint8_t *buffer, size_t max_bits);

// Initialize/clear allocation map
int stilaufs_alloc_init(void *buffer);

// Free an allocated index (clears the bit)
int stilaufs_alloc_free(void *buffer, size_t index);

// Allocate a free inode index (limits search to 128)
int stilaufs_alloc_inode(uint8_t *buffer);

// Allocate a free block index (limits search to 3805)
int stilaufs_alloc_block(uint8_t *buffer);

// Get allocation status of index (returns 1 if set, 0 if clear, -1 on error)
int stilaufs_alloc_status(uint8_t *buffer, size_t index);

// Get last used index in the bitmap
size_t stilaufs_alloc_last_used(uint8_t *buffer);

// Check if allocation map is full (up to 4096 bits)
bool stilaufs_alloc_is_full(uint8_t *buffer);

// Clear allocation map
int stilaufs_alloc_clear(uint8_t *buffer);

// Load allocation map (stub)
int stilaufs_alloc_load(uint8_t *buffer);

// Save allocation map (stub)
int stilaufs_alloc_save(uint8_t *buffer);

// Get allocation map size in bits
size_t stilaufs_alloc_size(uint8_t *buffer);

// Get allocation map stats
void stilaufs_alloc_stats(uint8_t *buffer, size_t *used, size_t *free);

// Get allocation map usage count (number of set bits)
size_t stilaufs_alloc_usage(uint8_t *buffer);

// Get allocation map free space in bits
size_t stilaufs_alloc_free_space(uint8_t *buffer);

// Get allocation map remaining space in bits
size_t stilaufs_alloc_remaining_space(uint8_t *buffer);

// Get allocation map available space in bits
size_t stilaufs_alloc_available_space(uint8_t *buffer);

// Get allocation map used space in bits
size_t stilaufs_alloc_used_space(uint8_t *buffer);

#endif // STILAUFS_ALLOC_H