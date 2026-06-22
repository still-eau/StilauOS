#include "vfs.h"
#include "stilaufs/super.h"
#include "stilaufs/inode.h"
#include "stilaufs/dir.h"
#include "stilaufs/alloc.h"
#include "../../Drivers/ahci.h"
#include "../mem/krnl_mm.h"
#include "../../Drivers/console.h"

// Global superblock representing mounted StilauFS
static stilaufs_superblock_t g_sb;

// Root and Current Working Directory pointers
static fs_node_t *g_root_node = NULL;
static fs_node_t *g_cwd_node = NULL;

// Rebuild in-memory fs_node_t hierarchy from disk inode table
static int vfs_reconstruct_tree(void)
{
    fs_node_t *node_map[STILAUFS_MAX_INODES];
    for (int i = 0; i < STILAUFS_MAX_INODES; i++)
    {
        node_map[i] = NULL;
    }

    // Set map for root directory
    node_map[0] = g_root_node;

    // First pass: instantiate all active nodes in memory
    for (uint32_t i = 1; i < STILAUFS_MAX_INODES; i++)
    {
        if (stilaufs_alloc_status((uint8_t *)g_sb.inode_bitmap, i) == 1)
        {
            stilaufs_inode_disk_t disk_inode;
            if (stilaufs_inode_read(&g_sb, i, &disk_inode) == 0 && disk_inode.used == 1)
            {
                fs_node_t *node = fs_node_create(disk_inode.type == FS_DIRECTORY ? FS_DIRECTORY : FS_FILE, disk_inode.name);
                if (node)
                {
                    node->inode = i;
                    node_map[i] = node;
                }
            }
        }
    }

    // Second pass: link child nodes to their respective parents
    for (uint32_t i = 1; i < STILAUFS_MAX_INODES; i++)
    {
        if (node_map[i] != NULL)
        {
            stilaufs_inode_disk_t disk_inode;
            if (stilaufs_inode_read(&g_sb, i, &disk_inode) == 0)
            {
                uint32_t parent_idx = disk_inode.parent_inode;
                if (parent_idx < STILAUFS_MAX_INODES && node_map[parent_idx] != NULL)
                {
                    fs_node_add_child(node_map[parent_idx], node_map[i]);
                }
            }
        }
    }

    return 0;
}

// VFS Initialization
int fs_init(void)
{
    // Find the first available AHCI drive
    ahci_device_t *dev = NULL;
    for (uint8_t i = 0; i < 32; i++)
    {
        dev = ahci_get_device(i);
        if (dev != NULL)
        {
            break;
        }
    }

    if (dev == NULL)
    {
        kprintf("[WARN] No AHCI storage devices detected. Filesystem cannot be mounted.\n");
        return -1;
    }

    kprintf("[INFO] Scanning drive on port %d (%s) for StilauFS...\n", dev->port_index, dev->model_number);

    bool must_format = false;
    
    // Try to read superblock
    if (stilaufs_read_superblock(&g_sb, dev) != 0)
    {
        kprintf("[INFO] Valid StilauFS signature not found. Formatting drive...\n");
        must_format = true;
    }

    if (must_format)
    {
        if (stilaufs_format(dev, 512) != 0)
        {
            kprintf("[ERROR] Failed to format disk.\n");
            return -1;
        }
        if (stilaufs_read_superblock(&g_sb, dev) != 0)
        {
            kprintf("[ERROR] Failed to mount formatted filesystem.\n");
            return -1;
        }
    }

    // Create root directory in-memory node
    g_root_node = fs_node_create(FS_DIRECTORY, "/");
    if (!g_root_node)
    {
        kprintf("[ERROR] Failed to allocate in-memory root node.\n");
        return -1;
    }
    g_root_node->inode = 0;
    g_cwd_node = g_root_node;

    if (must_format)
    {
        kprintf("[INFO] Creating default filesystem layout (/docs, /bin, /dev)...\n");
        // Create default directories
        fs_create_dir(g_root_node, "docs");
        fs_create_dir(g_root_node, "bin");
        fs_create_dir(g_root_node, "dev");

        // Create welcome file
        fs_node_t *docs = fs_find_node("/docs");
        if (docs)
        {
            fs_create_file(docs, "welcome.txt");
            fs_file_t *f = fs_fopen("/docs/welcome.txt", "w");
            if (f)
            {
                const char *msg = "Bienvenue sur StilauOS !\nLe systeme de fichiers StilauFS fonctionne parfaitement.\n";
                size_t msg_len = 0;
                while (msg[msg_len] != '\0') msg_len++;
                fs_write(f, msg, msg_len);
                fs_fclose(f);
            }
        }
    }
    else
    {
        // Reconstruct filesystem hierarchy in RAM
        vfs_reconstruct_tree();
    }

    kprintf("[OK] StilauFS mounted successfully.\n");
    return 0;
}

fs_node_t *fs_get_root(void)
{
    return g_root_node;
}

fs_node_t *fs_get_cwd(void)
{
    return g_cwd_node;
}

void fs_set_cwd(fs_node_t *node)
{
    if (node && fs_node_is_dir(node))
    {
        g_cwd_node = node;
    }
}

fs_node_t *fs_find_node(const char *path)
{
    if (!path || *path == '\0')
    {
        return NULL;
    }

    if (path[0] == '/')
    {
        return fs_lookup(g_root_node, path);
    }
    else
    {
        return fs_lookup(g_cwd_node, path);
    }
}

// VFS File Interface
fs_file_t *fs_fopen(const char *path, const char *mode)
{
    if (!path || !mode)
    {
        return NULL;
    }

    fs_node_t *node = fs_find_node(path);
    bool write_mode = (mode[0] == 'w');
    bool append_mode = (mode[0] == 'a');

    if (!node)
    {
        if (write_mode || append_mode)
        {
            // Create empty file
            char parent_path[MAX_PATH_LENGTH];
            path_get_parent(parent_path, path, MAX_PATH_LENGTH);
            const char *base = path_basename(path);

            fs_node_t *parent = fs_find_node(parent_path);
            if (!parent || !fs_create_file(parent, base))
            {
                return NULL;
            }

            node = fs_find_node(path);
            if (!node) return NULL;
        }
        else
        {
            return NULL; // Read mode, file doesn't exist
        }
    }

    if (fs_node_is_dir(node))
    {
        return NULL; // Cannot open directory as file
    }

    fs_file_t *file = kmalloc(sizeof(fs_file_t));
    if (!file)
    {
        return NULL;
    }

    file->node = node;
    file->offset = 0;
    file->flags = 0; // internal flags (can represent mode)

    if (write_mode)
    {
        // Truncate file on write mode
        stilaufs_inode_disk_t disk_inode;
        if (stilaufs_inode_read(&g_sb, node->inode, &disk_inode) == 0)
        {
            // Free any allocated data blocks
            bool modified = false;
            for (int i = 0; i < 13; i++)
            {
                if (disk_inode.direct[i] != 0)
                {
                    stilaufs_alloc_free(g_sb.block_bitmap, disk_inode.direct[i]);
                    disk_inode.direct[i] = 0;
                    g_sb.free_blocks++;
                    modified = true;
                }
            }
            if (modified)
            {
                ahci_write(g_sb.device, STILAUFS_DATA_BITMAP_LBA, 1, g_sb.block_bitmap);
            }
            disk_inode.size = 0;
            stilaufs_inode_write(&g_sb, node->inode, &disk_inode);
        }
    }
    else if (append_mode)
    {
        stilaufs_inode_disk_t disk_inode;
        if (stilaufs_inode_read(&g_sb, node->inode, &disk_inode) == 0)
        {
            file->offset = disk_inode.size;
        }
    }

    return file;
}

size_t fs_read(fs_file_t *file, void *buffer, size_t size)
{
    if (!file || !buffer || size == 0)
    {
        return 0;
    }

    stilaufs_inode_disk_t disk_inode;
    if (stilaufs_inode_read(&g_sb, file->node->inode, &disk_inode) != 0)
    {
        return 0;
    }

    if (file->offset >= disk_inode.size)
    {
        return 0; // EOF
    }

    size_t limit = disk_inode.size - file->offset;
    if (size > limit)
    {
        size = limit;
    }

    size_t bytes_read = 0;
    uint8_t sector_buf[512] __attribute__((aligned(16)));

    while (bytes_read < size)
    {
        size_t logical_block = (file->offset + bytes_read) / 512;
        size_t block_offset = (file->offset + bytes_read) % 512;
        size_t chunk_size = 512 - block_offset;

        if (chunk_size > (size - bytes_read))
        {
            chunk_size = size - bytes_read;
        }

        if (logical_block >= 13)
        {
            break; // Sanity check
        }

        uint32_t db_idx = disk_inode.direct[logical_block];
        if (db_idx == 0)
        {
            // Sparse block
            for (size_t i = 0; i < chunk_size; i++)
            {
                ((uint8_t *)buffer)[bytes_read + i] = 0;
            }
        }
        else
        {
            uint64_t lba = STILAUFS_DATA_BLOCKS_LBA + db_idx;
            if (ahci_read((ahci_device_t *)g_sb.device, lba, 1, sector_buf) != AHCI_SUCCESS)
            {
                break;
            }
            for (size_t i = 0; i < chunk_size; i++)
            {
                ((uint8_t *)buffer)[bytes_read + i] = sector_buf[block_offset + i];
            }
        }

        bytes_read += chunk_size;
    }

    file->offset += bytes_read;
    return bytes_read;
}

size_t fs_write(fs_file_t *file, const void *buffer, size_t size)
{
    if (!file || !buffer || size == 0)
    {
        return 0;
    }

    stilaufs_inode_disk_t disk_inode;
    if (stilaufs_inode_read(&g_sb, file->node->inode, &disk_inode) != 0)
    {
        return 0;
    }

    size_t bytes_written = 0;
    uint8_t sector_buf[512] __attribute__((aligned(16)));
    bool inode_dirty = false;

    while (bytes_written < size)
    {
        size_t logical_block = (file->offset + bytes_written) / 512;
        size_t block_offset = (file->offset + bytes_written) % 512;
        size_t chunk_size = 512 - block_offset;

        if (chunk_size > (size - bytes_written))
        {
            chunk_size = size - bytes_written;
        }

        if (logical_block >= 13)
        {
            kprintf("[WARN] StilauFS file size limit (6.5 KB) reached.\n");
            break; // Limit reached
        }

        // Allocate a new block if not present
        if (disk_inode.direct[logical_block] == 0)
        {
            int free_block = stilaufs_alloc_block((uint8_t *)g_sb.block_bitmap);
            if (free_block < 0)
            {
                kprintf("[WARN] StilauFS data block allocation failed: disk full.\n");
                break; // Disk full
            }

            // Persist bitmap immediately
            if (ahci_write((ahci_device_t *)g_sb.device, STILAUFS_DATA_BITMAP_LBA, 1, g_sb.block_bitmap) != AHCI_SUCCESS)
            {
                stilaufs_alloc_free(g_sb.block_bitmap, free_block);
                break;
            }

            disk_inode.direct[logical_block] = (uint32_t)free_block;
            g_sb.free_blocks--;
            inode_dirty = true;

            // Clear the new block on disk
            for (int i = 0; i < 512; i++) sector_buf[i] = 0;
            ahci_write((ahci_device_t *)g_sb.device, STILAUFS_DATA_BLOCKS_LBA + free_block, 1, sector_buf);
        }

        uint32_t db_idx = disk_inode.direct[logical_block];
        uint64_t lba = STILAUFS_DATA_BLOCKS_LBA + db_idx;

        if (chunk_size < 512)
        {
            // Read-modify-write for partial block
            if (ahci_read((ahci_device_t *)g_sb.device, lba, 1, sector_buf) != AHCI_SUCCESS)
            {
                break;
            }
            for (size_t i = 0; i < chunk_size; i++)
            {
                sector_buf[block_offset + i] = ((const uint8_t *)buffer)[bytes_written + i];
            }
            if (ahci_write((ahci_device_t *)g_sb.device, lba, 1, sector_buf) != AHCI_SUCCESS)
            {
                break;
            }
        }
        else
        {
            // Full block write
            for (int i = 0; i < 512; i++)
            {
                sector_buf[i] = ((const uint8_t *)buffer)[bytes_written + i];
            }
            if (ahci_write((ahci_device_t *)g_sb.device, lba, 1, sector_buf) != AHCI_SUCCESS)
            {
                break;
            }
        }

        bytes_written += chunk_size;
    }

    file->offset += bytes_written;
    if (file->offset > disk_inode.size)
    {
        disk_inode.size = file->offset;
        inode_dirty = true;
    }

    if (inode_dirty)
    {
        stilaufs_inode_write(&g_sb, file->node->inode, &disk_inode);
    }

    return bytes_written;
}

void fs_fclose(fs_file_t *file)
{
    if (file)
    {
        kfree(file);
    }
}

// VFS Directory Interface
fs_dir_t *fs_opendir(const char *path)
{
    fs_node_t *node = fs_find_node(path);
    if (!node || !fs_node_is_dir(node))
    {
        return NULL;
    }

    fs_dir_t *dir = kmalloc(sizeof(fs_dir_t));
    if (!dir)
    {
        return NULL;
    }

    dir->node = node;
    dir->current = node->first_child;
    return dir;
}

int fs_readdir(fs_dir_t *dir, dirent_t *entry)
{
    if (!dir || !entry || !dir->current)
    {
        return 0; // End of directory stream
    }

    fs_node_t *child = dir->current;
    
    // Copy node name
    size_t i;
    for (i = 0; i < MAX_NAME_LENGTH && child->name[i] != '\0'; i++)
    {
        entry->name[i] = child->name[i];
    }
    entry->name[i] = '\0';

    entry->type = child->type;

    // Read size from disk inode
    stilaufs_inode_disk_t disk_inode;
    if (stilaufs_inode_read(&g_sb, child->inode, &disk_inode) == 0)
    {
        entry->size = disk_inode.size;
    }
    else
    {
        entry->size = 0;
    }

    // Advance iterator to next child node
    dir->current = child->next_sibling;
    return 1;
}

void fs_closedir(fs_dir_t *dir)
{
    if (dir)
    {
        kfree(dir);
    }
}

// VFS Node Creation/Removal
int fs_create_file(fs_node_t *parent, const char *name)
{
    if (!parent || !name || !fs_node_is_dir(parent))
    {
        return 0;
    }

    uint32_t new_inode_idx;
    if (stilaufs_dir_add_entry(&g_sb, parent->inode, name, FS_FILE, &new_inode_idx) != 0)
    {
        return 0;
    }

    fs_node_t *child = fs_node_create(FS_FILE, name);
    if (!child)
    {
        // Rollback disk inode allocation on OOM
        stilaufs_inode_free(&g_sb, new_inode_idx);
        return 0;
    }

    child->inode = new_inode_idx;
    fs_node_add_child(parent, child);
    return 1;
}

int fs_create_dir(fs_node_t *parent, const char *name)
{
    if (!parent || !name || !fs_node_is_dir(parent))
    {
        return 0;
    }

    uint32_t new_inode_idx;
    if (stilaufs_dir_add_entry(&g_sb, parent->inode, name, FS_DIRECTORY, &new_inode_idx) != 0)
    {
        return 0;
    }

    fs_node_t *child = fs_node_create(FS_DIRECTORY, name);
    if (!child)
    {
        stilaufs_inode_free(&g_sb, new_inode_idx);
        return 0;
    }

    child->inode = new_inode_idx;
    fs_node_add_child(parent, child);
    return 1;
}

int fs_remove_node(fs_node_t *node)
{
    if (!node || node == g_root_node)
    {
        return -1;
    }

    fs_node_t *parent = node->parent;
    if (!parent)
    {
        return -1;
    }

    // Try to remove from the disk representation first
    if (stilaufs_dir_remove_entry(&g_sb, parent->inode, node->name) != 0)
    {
        return -1;
    }

    // Remove child from generic tree in memory
    fs_node_remove_child(parent, node);
    fs_node_destroy(node);

    return 0;
}
