#include "inode.h"
#include "alloc.h"
#include "../../../Drivers/ahci.h"

// Safe copy string helper
static void inode_strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++)
    {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Read an inode from disk by its index
int stilaufs_inode_read(stilaufs_superblock_t *sb, uint32_t index, stilaufs_inode_disk_t *out_inode)
{
    if (!sb || !out_inode || index >= STILAUFS_MAX_INODES)
    {
        return -1;
    }

    uint64_t lba = STILAUFS_INODE_TABLE_LBA + (index / 4);
    uint32_t offset = (index % 4) * 128;

    // Temporary buffer to hold the 512-byte sector
    uint8_t sector_buf[512] __attribute__((aligned(16)));

    if (ahci_read((ahci_device_t *)sb->device, lba, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // Copy 128 bytes of inode data
    for (int i = 0; i < 128; i++)
    {
        ((uint8_t *)out_inode)[i] = sector_buf[offset + i];
    }

    return 0;
}

// Write an inode to disk by its index
int stilaufs_inode_write(stilaufs_superblock_t *sb, uint32_t index, const stilaufs_inode_disk_t *inode)
{
    if (!sb || !inode || index >= STILAUFS_MAX_INODES)
    {
        return -1;
    }

    uint64_t lba = STILAUFS_INODE_TABLE_LBA + (index / 4);
    uint32_t offset = (index % 4) * 128;

    uint8_t sector_buf[512] __attribute__((aligned(16)));

    // Read existing sector first to preserve other inodes
    if (ahci_read((ahci_device_t *)sb->device, lba, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // Overlay the modified inode
    for (int i = 0; i < 128; i++)
    {
        sector_buf[offset + i] = ((const uint8_t *)inode)[i];
    }

    // Write back to disk
    if (ahci_write((ahci_device_t *)sb->device, lba, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    return 0;
}

// Allocate a new inode (marks in bitmap and writes initial inode structure)
int stilaufs_inode_alloc(stilaufs_superblock_t *sb, uint16_t type, const char *name, uint32_t parent_inode_idx, uint32_t *out_index)
{
    if (!sb || !name || !out_index)
    {
        return -1;
    }

    // Find free bit in inode bitmap
    int free_idx = stilaufs_alloc_inode((uint8_t *)sb->inode_bitmap);
    if (free_idx < 0)
    {
        return -1; // Disk full (out of inodes)
    }

    // Persist inode bitmap immediately
    if (ahci_write((ahci_device_t *)sb->device, STILAUFS_INODE_BITMAP_LBA, 1, sb->inode_bitmap) != AHCI_SUCCESS)
    {
        // Rollback bitmap assignment in memory
        stilaufs_alloc_free(sb->inode_bitmap, free_idx);
        return -1;
    }

    // Construct the new disk inode
    stilaufs_inode_disk_t disk_inode;
    for (int i = 0; i < 128; i++) ((uint8_t *)&disk_inode)[i] = 0; // Clear all fields

    disk_inode.size = 0;
    disk_inode.type = type;
    disk_inode.used = 1;
    disk_inode.parent_inode = parent_inode_idx;
    inode_strncpy(disk_inode.name, name, 60);

    // Write the new inode to disk
    if (stilaufs_inode_write(sb, (uint32_t)free_idx, &disk_inode) != 0)
    {
        // Rollback bitmap on failure
        stilaufs_alloc_free(sb->inode_bitmap, free_idx);
        ahci_write((ahci_device_t *)sb->device, STILAUFS_INODE_BITMAP_LBA, 1, sb->inode_bitmap);
        return -1;
    }

    *out_index = (uint32_t)free_idx;
    sb->free_inodes--;
    return 0;
}

// Free an inode and all its data blocks
int stilaufs_inode_free(stilaufs_superblock_t *sb, uint32_t index)
{
    if (!sb || index >= STILAUFS_MAX_INODES)
    {
        return -1;
    }

    // Check if the inode is actually in use
    if (stilaufs_alloc_status((uint8_t *)sb->inode_bitmap, index) != 1)
    {
        return 0; // Already free
    }

    stilaufs_inode_disk_t disk_inode;
    if (stilaufs_inode_read(sb, index, &disk_inode) != 0)
    {
        return -1;
    }

    // Free all data blocks associated with this inode
    bool blocks_modified = false;
    for (int i = 0; i < 13; i++)
    {
        if (disk_inode.direct[i] != 0)
        {
            // Clear the bit in block bitmap (block indices are 0-based relative to data blocks region)
            stilaufs_alloc_free(sb->block_bitmap, disk_inode.direct[i]);
            disk_inode.direct[i] = 0;
            blocks_modified = true;
            sb->free_blocks++;
        }
    }

    if (blocks_modified)
    {
        // Persist block bitmap
        if (ahci_write((ahci_device_t *)sb->device, STILAUFS_DATA_BITMAP_LBA, 1, sb->block_bitmap) != AHCI_SUCCESS)
        {
            return -1;
        }
    }

    // Clear inode fields on disk
    for (int i = 0; i < 128; i++) ((uint8_t *)&disk_inode)[i] = 0;
    disk_inode.used = 0;

    if (stilaufs_inode_write(sb, index, &disk_inode) != 0)
    {
        return -1;
    }

    // Clear inode bitmap entry
    stilaufs_alloc_free(sb->inode_bitmap, index);
    if (ahci_write((ahci_device_t *)sb->device, STILAUFS_INODE_BITMAP_LBA, 1, sb->inode_bitmap) != AHCI_SUCCESS)
    {
        return -1;
    }

    sb->free_inodes++;
    return 0;
}