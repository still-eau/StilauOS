#ifndef STILAUS_FS_SUPER_H
#define STILAUS_FS_SUPER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../fs_types.h"

// LBA sector layout of StilauFS on the disk image
#define STILAUFS_SUPERBLOCK_LBA     256
#define STILAUFS_INODE_TABLE_LBA    257
#define STILAUFS_INODE_BITMAP_LBA   289
#define STILAUFS_DATA_BITMAP_LBA    290
#define STILAUFS_DATA_BLOCKS_LBA    291

#define STILAUS_FS_MAGIC            0x53544C46  // "STLF"
#define STILAUS_FS_VERSION          1
#define STILAUS_FS_BLOCK_SIZE       512         // Block size is 1 sector (512 bytes)

// On-disk StilauFS superblock (stored at LBA 256)
typedef struct stilaufs_superblock_disk {
    uint32_t magic;             // filesystem signature ("STLF")
    uint32_t version;           // filesystem version (1)

    uint32_t block_size;        // size of a block in bytes (512)
    uint32_t inode_count;       // total inodes available (128)
    uint32_t block_count;       // total data blocks available (3805)

    uint32_t inode_table_start; // LBA block index of inode table (257)
    uint32_t data_block_start;  // LBA block index of data region (291)

    uint32_t root_inode;        // root directory inode index (0)

    uint32_t flags;             // feature flags (0)
} stilaufs_superblock_disk_t;

// In-memory superblock (runtime representation)
typedef struct stilaufs_superblock {
    stilaufs_superblock_disk_t disk;

    bool mounted;
    void *device;               // pointer to ahci_device_t block device driver

    uint32_t free_inodes;
    uint32_t free_blocks;

    void *inode_bitmap;         // 512-byte buffer representing inode allocation
    void *block_bitmap;         // 512-byte buffer representing data block allocation
} stilaufs_superblock_t;

// Load superblock and bitmaps from device
int stilaufs_read_superblock(stilaufs_superblock_t *sb, void *device);

// Write superblock back to device
int stilaufs_write_superblock(stilaufs_superblock_t *sb);

// Validate superblock magic/version
bool stilaufs_superblock_valid(const stilaufs_superblock_disk_t *sb);

// Initialize a fresh filesystem (format disk partition)
int stilaufs_format(void *device, uint32_t block_size);

// Mount StilauFS filesystem
int stilaufs_mount(void *device, stilaufs_superblock_t *sb);

// Unmount StilauFS filesystem
int stilaufs_unmount(stilaufs_superblock_t *sb);

#endif // STILAUS_FS_SUPER_H