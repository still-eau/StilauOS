#ifndef STILAUS_FS_DIR_H
#define STILAUS_FS_DIR_H

#include "../fs_types.h"
#include "super.h"
#include "inode.h"

// Add a directory entry (allocates an inode and links it under parent_idx via parent_inode field)
int stilaufs_dir_add_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name, uint16_t type, uint32_t *out_inode_idx);

// Remove a directory entry (deletes the inode and frees its blocks)
int stilaufs_dir_remove_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name);

// Find a directory entry by name
int stilaufs_dir_find_entry(stilaufs_superblock_t *sb, uint32_t parent_idx, const char *name, uint32_t *out_inode_idx);

#endif // STILAUS_FS_DIR_H
