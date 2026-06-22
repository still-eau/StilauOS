#include "super.h"
#include "inode.h"
#include "alloc.h"
#include "../../../Drivers/ahci.h"
#include "../../mem/krnl_mm.h"

// Safe copy string helper
static void super_strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++)
    {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}

// Read superblock from disk
int stilaufs_read_superblock(stilaufs_superblock_t *sb, void *device)
{
    if (!sb || !device)
    {
        return -1;
    }

    uint8_t sector_buf[512] __attribute__((aligned(16)));

    // Read superblock sector
    if (ahci_read((ahci_device_t *)device, STILAUFS_SUPERBLOCK_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // Copy to superblock disk structure
    const stilaufs_superblock_disk_t *disk_sb = (const stilaufs_superblock_disk_t *)sector_buf;
    if (!stilaufs_superblock_valid(disk_sb))
    {
        return -1;
    }

    // Copy fields
    for (size_t i = 0; i < sizeof(stilaufs_superblock_disk_t); i++)
    {
        ((uint8_t *)&sb->disk)[i] = ((const uint8_t *)disk_sb)[i];
    }

    sb->device = device;
    sb->mounted = true;

    // Allocate bitmap buffers
    sb->inode_bitmap = kmalloc(512);
    sb->block_bitmap = kmalloc(512);

    if (!sb->inode_bitmap || !sb->block_bitmap)
    {
        if (sb->inode_bitmap) kfree(sb->inode_bitmap);
        if (sb->block_bitmap) kfree(sb->block_bitmap);
        return -1;
    }

    // Read inode bitmap
    if (ahci_read((ahci_device_t *)device, STILAUFS_INODE_BITMAP_LBA, 1, sb->inode_bitmap) != AHCI_SUCCESS)
    {
        kfree(sb->inode_bitmap);
        kfree(sb->block_bitmap);
        return -1;
    }

    // Read data blocks bitmap
    if (ahci_read((ahci_device_t *)device, STILAUFS_DATA_BITMAP_LBA, 1, sb->block_bitmap) != AHCI_SUCCESS)
    {
        kfree(sb->inode_bitmap);
        kfree(sb->block_bitmap);
        return -1;
    }

    // Compute stats
    sb->free_inodes = STILAUFS_MAX_INODES - (uint32_t)stilaufs_alloc_usage((uint8_t *)sb->inode_bitmap);
    sb->free_blocks = STILAUFS_MAX_BLOCKS - (uint32_t)stilaufs_alloc_usage((uint8_t *)sb->block_bitmap);

    return 0;
}

// Write superblock back to device
int stilaufs_write_superblock(stilaufs_superblock_t *sb)
{
    if (!sb || !sb->device || !sb->mounted)
    {
        return -1;
    }

    uint8_t sector_buf[512] __attribute__((aligned(16)));

    // Zero out sector
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;

    // Copy disk superblock structure to sector
    for (size_t i = 0; i < sizeof(stilaufs_superblock_disk_t); i++)
    {
        sector_buf[i] = ((uint8_t *)&sb->disk)[i];
    }

    // Write back to LBA 256
    if (ahci_write((ahci_device_t *)sb->device, STILAUFS_SUPERBLOCK_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    return 0;
}

// Validate superblock magic/version
bool stilaufs_superblock_valid(const stilaufs_superblock_disk_t *sb)
{
    if (!sb) return false;
    return (sb->magic == STILAUS_FS_MAGIC &&
            sb->version == STILAUS_FS_VERSION &&
            sb->block_size == STILAUS_FS_BLOCK_SIZE);
}

// Initialize a fresh filesystem (format)
int stilaufs_format(void *device, uint32_t block_size)
{
    if (!device || block_size != STILAUS_FS_BLOCK_SIZE)
    {
        return -1;
    }

    // 1. Create superblock disk representation
    stilaufs_superblock_disk_t sb;
    sb.magic = STILAUS_FS_MAGIC;
    sb.version = STILAUS_FS_VERSION;
    sb.block_size = STILAUS_FS_BLOCK_SIZE;
    sb.inode_count = STILAUFS_MAX_INODES;
    sb.block_count = STILAUFS_MAX_BLOCKS;
    sb.inode_table_start = STILAUFS_INODE_TABLE_LBA;
    sb.data_block_start = STILAUFS_DATA_BLOCKS_LBA;
    sb.root_inode = 0;
    sb.flags = 0;

    uint8_t sector_buf[512] __attribute__((aligned(16)));

    // Write superblock
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;
    for (size_t i = 0; i < sizeof(stilaufs_superblock_disk_t); i++)
    {
        sector_buf[i] = ((uint8_t *)&sb)[i];
    }

    if (ahci_write((ahci_device_t *)device, STILAUFS_SUPERBLOCK_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // 2. Format the Root directory inode (index 0) at the first slot of the inode table (LBA 257)
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;

    stilaufs_inode_disk_t root_inode;
    for (int i = 0; i < 128; i++) ((uint8_t *)&root_inode)[i] = 0;

    root_inode.size = 0;
    root_inode.type = FS_DIRECTORY;
    root_inode.used = 1;
    root_inode.parent_inode = 0;
    super_strncpy(root_inode.name, "/", 60);

    // Overlay root_inode on the first 128 bytes of Sector 257 buffer
    for (int i = 0; i < 128; i++)
    {
        sector_buf[i] = ((uint8_t *)&root_inode)[i];
    }

    // Write Sector 257 (holds root inode and inodes 1,2,3 as unused)
    if (ahci_write((ahci_device_t *)device, STILAUFS_INODE_TABLE_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // Clear remaining sectors of the inode table (sectors 258 to 288)
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;
    for (uint64_t lba = STILAUFS_INODE_TABLE_LBA + 1; lba < STILAUFS_INODE_BITMAP_LBA; lba++)
    {
        if (ahci_write((ahci_device_t *)device, lba, 1, sector_buf) != AHCI_SUCCESS)
        {
            return -1;
        }
    }

    // 3. Format inode bitmap at LBA 289
    // Inode 0 is allocated, so set the first bit to 1 (0x01)
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;
    sector_buf[0] = 0x01;

    if (ahci_write((ahci_device_t *)device, STILAUFS_INODE_BITMAP_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    // 4. Format data blocks bitmap at LBA 290
    // All blocks initially free, so fill with 0
    for (int i = 0; i < 512; i++) sector_buf[i] = 0;

    if (ahci_write((ahci_device_t *)device, STILAUFS_DATA_BITMAP_LBA, 1, sector_buf) != AHCI_SUCCESS)
    {
        return -1;
    }

    return 0;
}

// Mount a StilauFS filesystem
int stilaufs_mount(void *device, stilaufs_superblock_t *sb)
{
    return stilaufs_read_superblock(sb, device);
}

// Unmount a StilauFS filesystem
int stilaufs_unmount(stilaufs_superblock_t *sb)
{
    if (!sb || !sb->mounted)
    {
        return -1;
    }

    if (sb->inode_bitmap) kfree(sb->inode_bitmap);
    if (sb->block_bitmap) kfree(sb->block_bitmap);

    sb->inode_bitmap = NULL;
    sb->block_bitmap = NULL;
    sb->mounted = false;

    return 0;
}