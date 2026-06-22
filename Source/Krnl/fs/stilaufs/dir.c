#include "dir.h"
#include "alloc.h"

// Freestanding string comparison helper
static int dir_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

// Find a directory entry by name
int stilaufs_dir_find_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name, uint32_t *out_inode_idx)
{
    if (!sb || !name || !out_inode_idx)
    {
        return -1;
    }

    stilaufs_inode_disk_t inode;
    for (uint32_t i = 0; i < STILAUFS_MAX_INODES; i++)
    {
        if (stilaufs_alloc_status((uint8_t *)sb->inode_bitmap, i) == 1)
        {
            if (stilaufs_inode_read(sb, i, &inode) == 0)
            {
                if (inode.used == 1 && inode.parent_inode == parent_idx && dir_strcmp(inode.name, name) == 0)
                {
                    *out_inode_idx = i;
                    return 0; // Found
                }
            }
        }
    }

    return -1; // Not found
}

// Add a directory entry (allocates an inode and links it under parent_idx via parent_inode field)
int stilaufs_dir_add_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name, uint16_t type, uint32_t *out_inode_idx)
{
    if (!sb || !name || !out_inode_idx)
    {
        return -1;
    }

    // Verify name length and check for empty/slash characters
    size_t len = 0;
    while (name[len] != '\0')
    {
        if (name[len] == '/') return -1;
        len++;
    }
    if (len == 0 || len >= 60) return -1;

    // Check if an entry with the same name already exists in this parent directory
    uint32_t existing_idx;
    if (stilaufs_dir_find_entry(sb, parent_idx, name, &existing_idx) == 0)
    {
        return -1; // Already exists
    }

    // Allocate and write the new inode
    if (stilaufs_inode_alloc(sb, type, name, parent_idx, out_inode_idx) != 0)
    {
        return -1; // Allocation failure
    }

    return 0;
}

// Remove a directory entry (deletes the inode and frees its blocks)
int stilaufs_dir_remove_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name)
{
    if (!sb || !name)
    {
        return -1;
    }

    uint32_t inode_idx;
    if (stilaufs_dir_find_entry(sb, parent_idx, name, &inode_idx) != 0)
    {
        return -1; // Entry not found
    }

    stilaufs_inode_disk_t inode;
    if (stilaufs_inode_read(sb, inode_idx, &inode) != 0)
    {
        return -1;
    }

    // If directory, ensure it is empty before removing
    if (inode.type == FS_DIRECTORY)
    {
        stilaufs_inode_disk_t child_inode;
        for (uint32_t i = 0; i < STILAUFS_MAX_INODES; i++)
        {
            if (i != inode_idx && stilaufs_alloc_status((uint8_t *)sb->inode_bitmap, i) == 1)
            {
                if (stilaufs_inode_read(sb, i, &child_inode) == 0)
                {
                    if (child_inode.used == 1 && child_inode.parent_inode == inode_idx)
                    {
                        return -1; // Directory is not empty
                    }
                }
            }
        }
    }

    // Free the inode and blocks
    return stilaufs_inode_free(sb, inode_idx);
}
