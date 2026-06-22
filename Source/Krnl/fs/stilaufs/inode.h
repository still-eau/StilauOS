// inode.h
//
// StilauFS inode implementation

#ifndef STILAUS_FS_INODE_H
#define STILAUS_FS_INODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../fs_types.h"
#include "super.h"

// On-disk StilauFS inode - exactly 128 bytes
typedef struct {
    uint32_t size;          // Size of the file in bytes
    uint16_t type;          // FS_FILE (0) or FS_DIRECTORY (1)
    uint16_t used;          // 1 if active, 0 if free
    uint32_t direct[13];    // 13 direct data block indices (relative to data_blocks_lba)
    uint32_t parent_inode;  // Index of parent inode
    char name[60];          // Name of file or directory (null-terminated)
    uint8_t padding[4];     // Padding to align to 128 bytes
} stilaufs_inode_disk_t;

// In-memory StilauFS inode representation
typedef struct {
    stilaufs_inode_disk_t disk;
    uint32_t index;        // Inode number / index in the inode table (0 to 127)
} stilaufs_inode_t;

// Read an inode from disk by its index
int stilaufs_inode_read(stilaufs_superblock_t *sb, uint32_t index, stilaufs_inode_disk_t *out_inode);

// Write an inode to disk by its index
int stilaufs_inode_write(stilaufs_superblock_t *sb, uint32_t index, const stilaufs_inode_disk_t *inode);

// Allocate a new inode (marks in bitmap and writes initial inode structure)
int stilaufs_inode_alloc(stilaufs_superblock_t *sb, uint16_t type, const char *name, uint32_t parent_inode_idx, uint32_t *out_index);

// Free an inode and all its data blocks
int stilaufs_inode_free(stilaufs_superblock_t *sb, uint32_t index);

#endif // STILAUS_FS_INODE_H