#include "alloc.h"

static void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
    {
        *p++ = (unsigned char)c;
    }
    return s;
}

// Find the first free bit (value 0), set it to 1, and return its index.
// Scanning is optimized using 64-bit word operations.
int stilaufs_alloc_bit(uint8_t *buffer, size_t max_bits)
{
    if (!buffer || max_bits == 0 || max_bits > STILAUFS_BITMAP_BITS)
    {
        return -1;
    }

    uint64_t *words = (uint64_t *)buffer;
    size_t num_words = (max_bits + 63) / 64;

    for (size_t w = 0; w < num_words; w++)
    {
        // If word is not fully allocated
        if (words[w] != 0xFFFFFFFFFFFFFFFFULL)
        {
            uint64_t inverted = ~words[w];
            int bit = __builtin_ctzll(inverted);
            size_t index = w * 64 + bit;

            if (index < max_bits)
            {
                words[w] |= (1ULL << bit);
                return (int)index;
            }
        }
    }

    return -1; // No free bits
}

int stilaufs_alloc_init(void *buffer)
{
    if (!buffer) return -1;
    memset(buffer, 0, STILAUFS_BITMAP_BYTES);
    return 0;
}

int stilaufs_alloc_free(void *buffer, size_t index)
{
    if (!buffer || index >= STILAUFS_BITMAP_BITS)
    {
        return -1;
    }

    uint8_t *map = (uint8_t *)buffer;
    map[index / 8] &= ~(1 << (index % 8));
    return 0;
}

int stilaufs_alloc_inode(uint8_t *buffer)
{
    return stilaufs_alloc_bit(buffer, STILAUFS_MAX_INODES);
}

int stilaufs_alloc_block(uint8_t *buffer)
{
    return stilaufs_alloc_bit(buffer, STILAUFS_MAX_BLOCKS);
}

int stilaufs_alloc_status(uint8_t *buffer, size_t index)
{
    if (!buffer || index >= STILAUFS_BITMAP_BITS)
    {
        return -1;
    }

    uint8_t *map = (uint8_t *)buffer;
    return (map[index / 8] & (1 << (index % 8))) ? 1 : 0;
}

size_t stilaufs_alloc_last_used(uint8_t *buffer)
{
    if (!buffer) return 0;

    uint64_t *words = (uint64_t *)buffer;
    size_t num_words = STILAUFS_BITMAP_BITS / 64;

    for (size_t w = num_words; w > 0; w--)
    {
        if (words[w - 1] != 0)
        {
            uint64_t val = words[w - 1];
            // Find leading 1-bit
            int bit = 63 - __builtin_clzll(val);
            return (w - 1) * 64 + bit;
        }
    }

    return 0;
}

bool stilaufs_alloc_is_full(uint8_t *buffer)
{
    if (!buffer) return true;

    uint64_t *words = (uint64_t *)buffer;
    size_t num_words = STILAUFS_BITMAP_BITS / 64;

    for (size_t w = 0; w < num_words; w++)
    {
        if (words[w] != 0xFFFFFFFFFFFFFFFFULL)
        {
            return false;
        }
    }

    return true;
}

int stilaufs_alloc_clear(uint8_t *buffer)
{
    if (!buffer) return -1;
    memset(buffer, 0, STILAUFS_BITMAP_BYTES);
    return 0;
}

int stilaufs_alloc_load(uint8_t *buffer)
{
    (void)buffer;
    return 0;
}

int stilaufs_alloc_save(uint8_t *buffer)
{
    (void)buffer;
    return 0;
}

size_t stilaufs_alloc_size(uint8_t *buffer)
{
    (void)buffer;
    return STILAUFS_BITMAP_BITS;
}

void stilaufs_alloc_stats(uint8_t *buffer, size_t *used, size_t *free)
{
    if (!buffer) return;

    size_t used_count = 0;
    uint64_t *words = (uint64_t *)buffer;
    size_t num_words = STILAUFS_BITMAP_BITS / 64;

    for (size_t w = 0; w < num_words; w++)
    {
        used_count += __builtin_popcountll(words[w]);
    }

    if (used) *used = used_count;
    if (free) *free = STILAUFS_BITMAP_BITS - used_count;
}

size_t stilaufs_alloc_usage(uint8_t *buffer)
{
    size_t used = 0;
    stilaufs_alloc_stats(buffer, &used, NULL);
    return used;
}

size_t stilaufs_alloc_free_space(uint8_t *buffer)
{
    size_t free_sp = 0;
    stilaufs_alloc_stats(buffer, NULL, &free_sp);
    return free_sp;
}

size_t stilaufs_alloc_remaining_space(uint8_t *buffer)
{
    return stilaufs_alloc_free_space(buffer);
}

size_t stilaufs_alloc_available_space(uint8_t *buffer)
{
    return stilaufs_alloc_free_space(buffer);
}

size_t stilaufs_alloc_used_space(uint8_t *buffer)
{
    return stilaufs_alloc_usage(buffer);
}