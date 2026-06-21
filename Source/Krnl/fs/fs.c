// fs.c - StilauFS custom persistent disk-backed file system
//
// Implements hierarchical tree-node operations synchronized with an on-disk
// filesystem (StilauFS) using polling ATA PIO sector operations.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fs.h"
#include "../mem/krnl_mm.h"
#include "../../Drivers/console.h"
#include "../../Drivers/io.h" // For inb, outb, inw, outw

// Root and Current Working Directory pointers
static fs_node_t *g_fs_root = NULL;
static fs_node_t *g_fs_cwd  = NULL;

// ---------------------------------------------------------------------------
// ATA PIO Driver
// ---------------------------------------------------------------------------

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

static void k_memcpy(void *dest, const void *src, size_t n)
{
    char *d = (char *)dest;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++)
    {
        d[i] = s[i];
    }
}

static void k_memset(void *dest, int val, size_t n)
{
    char *d = (char *)dest;
    for (size_t i = 0; i < n; i++)
    {
        d[i] = (char)val;
    }
}

void *memcpy(void *dest, const void *src, size_t n)
{
    k_memcpy(dest, src, n);
    return dest;
}

void *memset(void *dest, int val, size_t n)
{
    k_memset(dest, val, n);
    return dest;
}

// Local string utilities
static size_t k_strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
    {
        len++;
    }
    return len;
}

// Bounded copy: always null-terminates dest, truncates src if needed.
// Returns 0 on success, -1 if src was truncated.
static int k_strlcpy(char *dest, const char *src, size_t dest_size)
{
    if (dest_size == 0)
    {
        return -1;
    }

    size_t i = 0;
    while (i < dest_size - 1 && src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';

    return (src[i] != '\0') ? -1 : 0;
}

static void k_strncpy(char *dest, const char *src, size_t n)
{
    size_t i = 0;
    while (i < n && src[i] != '\0')
    {
        dest[i] = src[i];
        i++;
    }
    while (i < n)
    {
        dest[i] = '\0';
        i++;
    }
}

static int k_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

// Wait until ATA drive is not busy
static void ata_wait_ready(void)
{
    while (inb(0x1F7) & ATA_STATUS_BSY)
    {
        // Spin wait
    }
}

// Wait until ATA drive has data ready (DRQ set)
static int ata_wait_drq(void)
{
    while (1)
    {
        uint8_t status = inb(0x1F7);
        if (status & ATA_STATUS_ERR)
        {
            return -1;
        }
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ))
        {
            return 0;
        }
    }
}

static int ata_read_sector(uint32_t lba, uint8_t *buf)
{
    ata_wait_ready();

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1); // Read 1 sector
    outb(0x1F3, (uint8_t)(lba & 0xFF));
    outb(0x1F4, (uint8_t)((lba >> 8) & 0xFF));
    outb(0x1F5, (uint8_t)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x20); // Command: Read Sectors with retry

    if (ata_wait_drq() < 0)
    {
        return -1;
    }

    uint16_t *ptr = (uint16_t *)buf;
    for (int i = 0; i < 256; i++)
    {
        ptr[i] = inw(0x1F0);
    }

    return 0;
}

static int ata_write_sector(uint32_t lba, const uint8_t *buf)
{
    ata_wait_ready();

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1); // Write 1 sector
    outb(0x1F3, (uint8_t)(lba & 0xFF));
    outb(0x1F4, (uint8_t)((lba >> 8) & 0xFF));
    outb(0x1F5, (uint8_t)((lba >> 16) & 0xFF));
    outb(0x1F7, 0x30); // Command: Write Sectors with retry

    if (ata_wait_drq() < 0)
    {
        return -1;
    }

    const uint16_t *ptr = (const uint16_t *)buf;
    for (int i = 0; i < 256; i++)
    {
        outw(0x1F0, ptr[i]);
    }

    // Send ATA cache flush command
    outb(0x1F7, 0xE7);
    ata_wait_ready();

    return 0;
}

// ---------------------------------------------------------------------------
// StilauFS On-Disk Structure Definitions
// ---------------------------------------------------------------------------

#define STILAUFS_START_LBA      256
#define STILAUFS_MAGIC          0x53544C46 // "STLF"
#define STILAUFS_NUM_INODES     128
#define STILAUFS_MAX_NAME       60
#define STILAUFS_DIRECT_BLOCKS  13

typedef struct {
    uint32_t magic;
    uint32_t total_sectors;
    uint32_t num_inodes;
    uint32_t num_data_blocks;
    uint32_t inode_table_lba;
    uint32_t inode_bitmap_lba;
    uint32_t data_bitmap_lba;
    uint32_t data_blocks_lba;
} stilaufs_superblock_t;

typedef struct {
    uint32_t size;                          // File size in bytes
    uint16_t type;                          // FS_FILE or FS_DIRECTORY
    uint16_t used;                          // 1 if used, 0 if free
    uint32_t direct[STILAUFS_DIRECT_BLOCKS];// Direct blocks indices
    uint32_t parent_inode;                  // Inode index of parent node
    char name[STILAUFS_MAX_NAME];           // Name of the node
    uint8_t padding[4];                     // Aligns structure to exactly 128 bytes
} stilaufs_disk_inode_t;

// In-Memory cache of StilauFS structures
static stilaufs_superblock_t g_sb;
static stilaufs_disk_inode_t g_inodes[STILAUFS_NUM_INODES];
static uint8_t g_inode_bitmap[512]; // 1 sector = 4096 bits (we only use first 128)
static uint8_t g_data_bitmap[512];  // 1 sector = 4096 bits (we use up to 3000)

// Helper functions for disk metadata sync
static void write_superblock(void)
{
    uint8_t sector[512] = {0};
    k_memcpy(sector, &g_sb, sizeof(g_sb));
    ata_write_sector(STILAUFS_START_LBA, sector);
}

static void read_superblock(void)
{
    uint8_t sector[512];
    ata_read_sector(STILAUFS_START_LBA, sector);
    k_memcpy(&g_sb, sector, sizeof(g_sb));
}

static void write_inode_table(void)
{
    // 128 inodes * 128 bytes = 16384 bytes = 32 sectors
    // 4 inodes per sector
    for (int i = 0; i < STILAUFS_NUM_INODES / 4; i++)
    {
        ata_write_sector(g_sb.inode_table_lba + i, (const uint8_t *)&g_inodes[i * 4]);
    }
}

static void read_inode_table(void)
{
    for (int i = 0; i < STILAUFS_NUM_INODES / 4; i++)
    {
        ata_read_sector(g_sb.inode_table_lba + i, (uint8_t *)&g_inodes[i * 4]);
    }
}

static void write_bitmaps(void)
{
    ata_write_sector(g_sb.inode_bitmap_lba, g_inode_bitmap);
    ata_write_sector(g_sb.data_bitmap_lba, g_data_bitmap);
}

static void read_bitmaps(void)
{
    ata_read_sector(g_sb.inode_bitmap_lba, g_inode_bitmap);
    ata_read_sector(g_sb.data_bitmap_lba, g_data_bitmap);
}

// Traverse the in-memory fs_node tree to locate node by its inode index
static fs_node_t *find_fs_node_by_inode(fs_node_t *curr, uint32_t inode_idx)
{
    if (!curr)
    {
        return NULL;
    }
    if (curr->inode_idx == inode_idx)
    {
        return curr;
    }
    if (curr->type == FS_DIRECTORY)
    {
        fs_node_t *child = curr->dir.head;
        while (child)
        {
            fs_node_t *found = find_fs_node_by_inode(child, inode_idx);
            if (found)
            {
                return found;
            }
            child = child->next;
        }
    }
    return NULL;
}

// Locate first available inode on disk
static int allocate_free_inode(void)
{
    for (int i = 0; i < STILAUFS_NUM_INODES; i++)
    {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        if (!(g_inode_bitmap[byte_idx] & (1 << bit_idx)))
        {
            g_inode_bitmap[byte_idx] |= (1 << bit_idx);
            write_bitmaps();
            return i;
        }
    }
    return -1;
}

// Synchronize node size, direct block map, and file data back to disk
static void sync_node_to_disk(fs_node_t *node)
{
    if (!node || node->inode_idx >= STILAUFS_NUM_INODES)
    {
        return;
    }

    stilaufs_disk_inode_t *disk_inode = &g_inodes[node->inode_idx];

    // 1. Free existing data blocks allocated to this inode in the bitmap
    int old_blocks = (disk_inode->size + 512 - 1) / 512;
    for (int i = 0; i < old_blocks; i++)
    {
        uint32_t db_idx = disk_inode->direct[i];
        if (db_idx < g_sb.num_data_blocks)
        {
            g_data_bitmap[db_idx / 8] &= ~(1 << (db_idx % 8));
        }
        disk_inode->direct[i] = 0xFFFFFFFF; // mark unused
    }

    disk_inode->size = node->size;

    // 2. Allocate and write new data blocks
    int new_blocks = (node->size + 512 - 1) / 512;
    if (new_blocks > STILAUFS_DIRECT_BLOCKS)
    {
        new_blocks = STILAUFS_DIRECT_BLOCKS;
    }

    const char *src = (node->type == FS_FILE) ? node->data : NULL;

    for (int i = 0; i < new_blocks; i++)
    {
        // Find a free data block index
        int db_idx = -1;
        for (uint32_t b = 0; b < g_sb.num_data_blocks; b++)
        {
            if (!(g_data_bitmap[b / 8] & (1 << (b % 8))))
            {
                db_idx = b;
                break;
            }
        }

        if (db_idx == -1)
        {
            // Disk full, cap sizing here
            disk_inode->size = i * 512;
            node->size = i * 512;
            break;
        }

        // Mark block as used
        g_data_bitmap[db_idx / 8] |= (1 << (db_idx % 8));
        disk_inode->direct[i] = db_idx;

        // Write sector to disk
        uint8_t temp[512] = {0};
        int to_copy = node->size - (i * 512);
        if (to_copy > 512)
        {
            to_copy = 512;
        }
        if (to_copy < 0)
        {
            to_copy = 0;
        }

        if (src && to_copy > 0)
        {
            k_memcpy(temp, src + (i * 512), to_copy);
        }

        ata_write_sector(g_sb.data_blocks_lba + db_idx, temp);
    }

    // 3. Write metadata tables back to disk
    write_inode_table();
    write_bitmaps();
}

// ---------------------------------------------------------------------------
// File System Core API Implementation
// ---------------------------------------------------------------------------

fs_node_t *fs_get_root(void)
{
    return g_fs_root;
}

fs_node_t *fs_get_cwd(void)
{
    return g_fs_cwd;
}

void fs_set_cwd(fs_node_t *node)
{
    if (node && node->type == FS_DIRECTORY)
    {
        g_fs_cwd = node;
    }
}

// Format the disk with StilauFS structures
static void fs_format(void)
{
    k_serial_puts("[STILAUFS] No valid filesystem found. Formatting disk...\n");

    // Setup superblock
    g_sb.magic            = STILAUFS_MAGIC;
    g_sb.total_sectors    = 3840; // 4096 - 256
    g_sb.num_inodes       = STILAUFS_NUM_INODES;
    g_sb.num_data_blocks  = 3000;
    g_sb.inode_table_lba  = STILAUFS_START_LBA + 1; // 257 to 288
    g_sb.inode_bitmap_lba = STILAUFS_START_LBA + 33; // 289
    g_sb.data_bitmap_lba  = STILAUFS_START_LBA + 34; // 290
    g_sb.data_blocks_lba  = STILAUFS_START_LBA + 35; // 291 onwards

    k_memset(g_inode_bitmap, 0, sizeof(g_inode_bitmap));
    k_memset(g_data_bitmap, 0, sizeof(g_data_bitmap));
    k_memset(g_inodes, 0, sizeof(g_inodes));

    // Initialize root inode 0
    g_inodes[0].used         = 1;
    g_inodes[0].type         = FS_DIRECTORY;
    g_inodes[0].parent_inode = 0;
    g_inodes[0].size         = 0;
    k_strlcpy(g_inodes[0].name, "/", STILAUFS_MAX_NAME);
    for (int j = 0; j < STILAUFS_DIRECT_BLOCKS; j++)
    {
        g_inodes[0].direct[j] = 0xFFFFFFFF;
    }

    // Set bit 0 in inode bitmap
    g_inode_bitmap[0] = 1;

    // Write all tables to disk
    write_superblock();
    write_inode_table();
    write_bitmaps();

    // Recreate the root node in memory
    g_fs_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!g_fs_root)
    {
        k_serial_puts("FS ERROR: Failed to allocate root filesystem node!\n");
        return;
    }

    k_strlcpy(g_fs_root->name, "/", FS_NAME_MAX);
    g_fs_root->type      = FS_DIRECTORY;
    g_fs_root->size      = 0;
    g_fs_root->dir.head  = NULL;
    g_fs_root->parent    = NULL;
    g_fs_root->next      = NULL;
    g_fs_root->inode_idx = 0;

    g_fs_cwd = g_fs_root;

    // Create system default directories
    fs_node_t *docs = fs_create_dir(g_fs_root, "docs");
    if (!docs)
    {
        k_serial_puts("FS WARNING: Failed to create /docs\n");
    }

    fs_node_t *bin = fs_create_dir(g_fs_root, "bin");
    if (!bin)
    {
        k_serial_puts("FS WARNING: Failed to create /bin\n");
    }
    else
    {
        fs_node_t *f;
        f = fs_create_file(bin, "ls");    if (f) fs_write_file(f, "StilauOS Command: ls\n", 19);
        f = fs_create_file(bin, "cat");   if (f) fs_write_file(f, "StilauOS Command: cat\n", 20);
        f = fs_create_file(bin, "mkdir"); if (f) fs_write_file(f, "StilauOS Command: mkdir\n", 22);
        f = fs_create_file(bin, "touch"); if (f) fs_write_file(f, "StilauOS Command: touch\n", 22);
        f = fs_create_file(bin, "write"); if (f) fs_write_file(f, "StilauOS Command: write\n", 22);
        f = fs_create_file(bin, "rm");    if (f) fs_write_file(f, "StilauOS Command: rm\n", 19);
        f = fs_create_file(bin, "cd");    if (f) fs_write_file(f, "StilauOS Command: cd\n", 19);
        f = fs_create_file(bin, "pwd");   if (f) fs_write_file(f, "StilauOS Command: pwd\n", 20);
        f = fs_create_file(bin, "hello"); if (f) fs_write_file(f, "StilauOS Command: hello\n", 22);
        f = fs_create_file(bin, "halt");  if (f) fs_write_file(f, "StilauOS Command: halt\n", 21);
    }

    if (!fs_create_dir(g_fs_root, "dev"))
    {
        k_serial_puts("FS WARNING: Failed to create /dev\n");
    }

    // Add a welcome readme file in the docs directory
    if (docs)
    {
        fs_node_t *welcome = fs_create_file(docs, "welcome.txt");
        if (welcome)
        {
            const char *msg = "Bienvenue sur StilauOS !\n"
                              "Ce systeme de fichiers (StilauFS) est maintenant persistant.\n"
                              "Vous pouvez lister les repertoires avec la commande 'ls',\n"
                              "creer des dossiers avec 'mkdir', des fichiers avec 'touch',\n"
                              "ecrire avec 'write' et lire avec 'cat'.\n"
                              "Toutes vos modifications seront sauvegardees sur le disque !\n";
            if (fs_write_file(welcome, msg, k_strlen(msg)) != 0)
            {
                k_serial_puts("FS WARNING: Failed to write welcome.txt\n");
            }
        }
    }

    k_serial_puts("[STILAUFS] Persistent filesystem formatted and initialized.\n");
}

// Load filesystem and reconstruct the in-memory tree from disk
static void fs_mount(void)
{
    k_serial_puts("[STILAUFS] Valid filesystem detected. Loading structures...\n");

    // Load tables
    read_inode_table();
    read_bitmaps();

    // Reconstruct root node
    g_fs_root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!g_fs_root)
    {
        k_serial_puts("FS ERROR: Failed to allocate root node!\n");
        return;
    }

    k_strlcpy(g_fs_root->name, "/", FS_NAME_MAX);
    g_fs_root->type      = FS_DIRECTORY;
    g_fs_root->size      = 0;
    g_fs_root->dir.head  = NULL;
    g_fs_root->parent    = NULL;
    g_fs_root->next      = NULL;
    g_fs_root->inode_idx = 0;

    g_fs_cwd = g_fs_root;

    // Multi-pass reconstruction of the directory tree
    bool added_any = true;
    bool loaded[STILAUFS_NUM_INODES] = {false};
    loaded[0] = true; // Root is loaded

    while (added_any)
    {
        added_any = false;
        for (uint32_t i = 1; i < STILAUFS_NUM_INODES; i++)
        {
            if (!loaded[i] && g_inodes[i].used)
            {
                // Find parent node in our reconstructed tree
                fs_node_t *parent_node = find_fs_node_by_inode(g_fs_root, g_inodes[i].parent_inode);
                if (parent_node)
                {
                    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
                    if (new_node)
                    {
                        k_strlcpy(new_node->name, g_inodes[i].name, FS_NAME_MAX);
                        new_node->type      = (fs_node_type_t)g_inodes[i].type;
                        new_node->size      = g_inodes[i].size;
                        new_node->inode_idx = i;
                        new_node->parent    = parent_node;
                        new_node->next      = NULL;

                        if (new_node->type == FS_DIRECTORY)
                        {
                            new_node->dir.head = NULL;
                        }
                        else
                        {
                            new_node->data = NULL;
                            if (new_node->size > 0)
                            {
                                new_node->data = (char *)kmalloc(new_node->size + 1);
                                if (new_node->data)
                                {
                                    int blocks = (new_node->size + 512 - 1) / 512;
                                    if (blocks > STILAUFS_DIRECT_BLOCKS)
                                    {
                                        blocks = STILAUFS_DIRECT_BLOCKS;
                                    }
                                    for (int b = 0; b < blocks; b++)
                                    {
                                        uint32_t db_idx = g_inodes[i].direct[b];
                                        uint8_t temp[512];
                                        ata_read_sector(g_sb.data_blocks_lba + db_idx, temp);

                                        int to_copy = new_node->size - (b * 512);
                                        if (to_copy > 512)
                                        {
                                            to_copy = 512;
                                        }
                                        k_memcpy(new_node->data + (b * 512), temp, to_copy);
                                    }
                                    new_node->data[new_node->size] = '\0';
                                }
                            }
                        }

                        // Link child to parent list
                        if (parent_node->dir.head == NULL)
                        {
                            parent_node->dir.head = new_node;
                        }
                        else
                        {
                            fs_node_t *sibling = parent_node->dir.head;
                            while (sibling->next != NULL)
                            {
                                sibling = sibling->next;
                            }
                            sibling->next = new_node;
                        }

                        loaded[i] = true;
                        added_any = true;
                    }
                }
            }
        }
    }

    k_serial_puts("[STILAUFS] Persistent filesystem successfully loaded.\n");
}

void fs_init(void)
{
    // Try to read superblock from disk at start sector
    read_superblock();

    if (g_sb.magic == STILAUFS_MAGIC)
    {
        fs_mount();
    }
    else
    {
        fs_format();
    }
}

fs_node_t *fs_create_dir(fs_node_t *parent, const char *name)
{
    if (!parent || parent->type != FS_DIRECTORY || !name || k_strlen(name) == 0)
    {
        return NULL;
    }

    if (k_strlen(name) >= FS_NAME_MAX)
    {
        return NULL;
    }

    for (const char *p = name; *p; p++)
    {
        if (*p == '/')
        {
            return NULL;
        }
    }

    // Check if node name already exists in parent directory
    fs_node_t *curr = parent->dir.head;
    while (curr)
    {
        if (k_strcmp(curr->name, name) == 0)
        {
            return NULL;
        }
        curr = curr->next;
    }

    // Allocate inode index on disk
    int idx = allocate_free_inode();
    if (idx < 0)
    {
        return NULL; // Inode table full
    }

    // Configure disk inode
    g_inodes[idx].used         = 1;
    g_inodes[idx].type         = FS_DIRECTORY;
    g_inodes[idx].parent_inode = parent->inode_idx;
    g_inodes[idx].size         = 0;
    k_strlcpy(g_inodes[idx].name, name, STILAUFS_MAX_NAME);
    for (int j = 0; j < STILAUFS_DIRECT_BLOCKS; j++)
    {
        g_inodes[idx].direct[j] = 0xFFFFFFFF;
    }

    write_inode_table();

    // Create in-memory node representation
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node)
    {
        return NULL;
    }

    k_strlcpy(new_node->name, name, FS_NAME_MAX);
    new_node->type      = FS_DIRECTORY;
    new_node->size      = 0;
    new_node->dir.head  = NULL;
    new_node->parent    = parent;
    new_node->next      = NULL;
    new_node->inode_idx = idx;

    // Append child node to parent
    if (parent->dir.head == NULL)
    {
        parent->dir.head = new_node;
    }
    else
    {
        fs_node_t *temp = parent->dir.head;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = new_node;
    }

    return new_node;
}

fs_node_t *fs_create_file(fs_node_t *parent, const char *name)
{
    if (!parent || parent->type != FS_DIRECTORY || !name || k_strlen(name) == 0)
    {
        return NULL;
    }

    if (k_strlen(name) >= FS_NAME_MAX)
    {
        return NULL;
    }

    for (const char *p = name; *p; p++)
    {
        if (*p == '/')
        {
            return NULL;
        }
    }

    // Check if node name already exists in parent directory
    fs_node_t *curr = parent->dir.head;
    while (curr)
    {
        if (k_strcmp(curr->name, name) == 0)
        {
            return NULL;
        }
        curr = curr->next;
    }

    // Allocate inode index on disk
    int idx = allocate_free_inode();
    if (idx < 0)
    {
        return NULL; // Inode table full
    }

    // Configure disk inode
    g_inodes[idx].used         = 1;
    g_inodes[idx].type         = FS_FILE;
    g_inodes[idx].parent_inode = parent->inode_idx;
    g_inodes[idx].size         = 0;
    k_strlcpy(g_inodes[idx].name, name, STILAUFS_MAX_NAME);
    for (int j = 0; j < STILAUFS_DIRECT_BLOCKS; j++)
    {
        g_inodes[idx].direct[j] = 0xFFFFFFFF;
    }

    write_inode_table();

    // Create in-memory node representation
    fs_node_t *new_node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!new_node)
    {
        return NULL;
    }

    k_strlcpy(new_node->name, name, FS_NAME_MAX);
    new_node->type      = FS_FILE;
    new_node->size      = 0;
    new_node->data      = NULL;
    new_node->parent    = parent;
    new_node->next      = NULL;
    new_node->inode_idx = idx;

    // Append child node to parent
    if (parent->dir.head == NULL)
    {
        parent->dir.head = new_node;
    }
    else
    {
        fs_node_t *temp = parent->dir.head;
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = new_node;
    }

    return new_node;
}

int fs_write_file(fs_node_t *node, const char *data, size_t size)
{
    if (!node || node->type != FS_FILE)
    {
        return -1;
    }

    // Free existing data buffer in RAM
    if (node->data)
    {
        kfree(node->data);
        node->data = NULL;
        node->size = 0;
    }

    if (size > 0 && data)
    {
        node->data = (char *)kmalloc(size + 1);
        if (!node->data)
        {
            return -1;
        }
        for (size_t i = 0; i < size; i++)
        {
            node->data[i] = data[i];
        }
        node->data[size] = '\0'; // Ensure null-termination
        node->size       = size;
    }

    // Sync to disk
    sync_node_to_disk(node);

    return 0;
}

static bool fs_is_ancestor_or_self(fs_node_t *node, fs_node_t *candidate)
{
    if (!node || !candidate)
    {
        return false;
    }

    if (node->type != FS_DIRECTORY)
    {
        return node == candidate;
    }

    if (node == candidate)
    {
        return true;
    }

    fs_node_t *child = node->dir.head;
    while (child)
    {
        if (fs_is_ancestor_or_self(child, candidate))
        {
            return true;
        }
        child = child->next;
    }

    return false;
}

int fs_remove_node(fs_node_t *node)
{
    if (!node || node == g_fs_root)
    {
        return -1;
    }

    // If active working directory is affected, update to parent or root
    if (g_fs_cwd && fs_is_ancestor_or_self(node, g_fs_cwd))
    {
        g_fs_cwd = node->parent ? node->parent : g_fs_root;
    }

    // Recursively delete children if directory
    if (node->type == FS_DIRECTORY)
    {
        fs_node_t *curr = node->dir.head;
        while (curr)
        {
            fs_node_t *next = curr->next;
            fs_remove_node(curr);
            curr = next;
        }
        node->dir.head = NULL;
    }
    else if (node->type == FS_FILE)
    {
        if (node->data)
        {
            kfree(node->data);
            node->data = NULL;
        }
    }

    // 1. Release disk sectors and disk inode
    uint32_t idx = node->inode_idx;
    if (idx < STILAUFS_NUM_INODES)
    {
        stilaufs_disk_inode_t *disk_inode = &g_inodes[idx];
        
        // Free data blocks
        int blocks = (disk_inode->size + 512 - 1) / 512;
        for (int i = 0; i < blocks; i++)
        {
            uint32_t db_idx = disk_inode->direct[i];
            if (db_idx < g_sb.num_data_blocks)
            {
                g_data_bitmap[db_idx / 8] &= ~(1 << (db_idx % 8));
            }
        }
        
        // Clear inode definition
        k_memset(disk_inode, 0, sizeof(stilaufs_disk_inode_t));
        
        // Clear bitmap bit
        g_inode_bitmap[idx / 8] &= ~(1 << (idx % 8));

        write_inode_table();
        write_bitmaps();
    }

    // Unlink node from parent children list
    fs_node_t *parent = node->parent;
    if (parent)
    {
        if (parent->dir.head == node)
        {
            parent->dir.head = node->next;
        }
        else
        {
            fs_node_t *prev = parent->dir.head;
            while (prev->next && prev->next != node)
            {
                prev = prev->next;
            }
            if (prev->next == node)
            {
                prev->next = node->next;
            }
        }
    }

    kfree(node);
    return 0;
}

fs_node_t *fs_find_node(const char *path)
{
    if (!path || k_strlen(path) == 0)
    {
        return NULL;
    }

    fs_node_t *curr = NULL;

    // Start traversal at root or cwd
    if (path[0] == '/')
    {
        curr = g_fs_root;
        while (*path == '/')
        {
            path++;
        }
    }
    else
    {
        curr = g_fs_cwd;
    }

    if (*path == '\0')
    {
        return curr;
    }

    if (k_strlen(path) >= FS_PATH_MAX)
    {
        return NULL;
    }

    char temp_path[FS_PATH_MAX];
    k_strncpy(temp_path, path, sizeof(temp_path) - 1);
    temp_path[sizeof(temp_path) - 1] = '\0';

    char *token_start = temp_path;
    char *p = temp_path;

    while (curr != NULL)
    {
        while (*p != '\0' && *p != '/')
        {
            p++;
        }

        bool final_token = (*p == '\0');
        if (*p == '/')
        {
            *p = '\0';
        }

        if (k_strlen(token_start) == 0)
        {
            // Do nothing
        }
        else if (k_strcmp(token_start, ".") == 0)
        {
            // Stay on current node
        }
        else if (k_strcmp(token_start, "..") == 0)
        {
            if (curr->parent != NULL)
            {
                curr = curr->parent;
            }
        }
        else
        {
            if (curr->type != FS_DIRECTORY)
            {
                return NULL;
            }

            fs_node_t *child = curr->dir.head;
            fs_node_t *found = NULL;
            while (child != NULL)
            {
                if (k_strcmp(child->name, token_start) == 0)
                {
                    found = child;
                    break;
                }
                child = child->next;
            }
            curr = found;
        }

        if (final_token)
        {
            break;
        }

        p++;
        token_start = p;
    }

    return curr;
}

// ---------------------------------------------------------------------------
// Directory Iteration
// ---------------------------------------------------------------------------

fs_dir_t *fs_opendir(const char *path)
{
    if (!path)
    {
        return NULL;
    }

    fs_node_t *node = fs_find_node(path);
    if (!node || node->type != FS_DIRECTORY)
    {
        return NULL;
    }

    fs_dir_t *dirp = (fs_dir_t *)kmalloc(sizeof(fs_dir_t));
    if (!dirp)
    {
        return NULL;
    }

    dirp->dir_node = node;
    dirp->cursor   = node->dir.head;

    return dirp;
}

void fs_closedir(fs_dir_t *dirp)
{
    if (dirp)
    {
        kfree(dirp);
    }
}

dirent_t *fs_readdir(fs_dir_t *dirp, dirent_t *entry_out)
{
    if (!dirp || !entry_out)
    {
        return NULL;
    }

    if (dirp->cursor == NULL)
    {
        entry_out->name[0] = '\0';
        return NULL;
    }

    fs_node_t *current = dirp->cursor;

    k_strlcpy(entry_out->name, current->name, FS_NAME_MAX);
    entry_out->type = (uint32_t)current->type;
    entry_out->size = (uint32_t)current->size;

    dirp->cursor = current->next;

    return entry_out;
}

// ---------------------------------------------------------------------------
// POSIX File Interface
// ---------------------------------------------------------------------------

fs_file_t *fs_fopen(const char *path, const char *mode)
{
    if (!path || !mode)
    {
        return NULL;
    }

    fs_node_t *node = fs_find_node(path);
    int writing = 0;

    if (k_strcmp(mode, "w") == 0)
    {
        writing = 1;
        if (!node)
        {
            // Resolve parent node path and new node name
            char name[FS_NAME_MAX];
            int last_slash = -1;
            int len = 0;
            while (path[len])
            {
                if (path[len] == '/')
                {
                    last_slash = len;
                }
                len++;
            }

            fs_node_t *parent = NULL;
            if (last_slash == -1)
            {
                k_strlcpy(name, path, FS_NAME_MAX);
                parent = g_fs_cwd;
            }
            else if (last_slash == 0)
            {
                k_strlcpy(name, path + 1, FS_NAME_MAX);
                parent = g_fs_root;
            }
            else
            {
                char parent_path[FS_PATH_MAX];
                k_strncpy(parent_path, path, last_slash);
                parent_path[last_slash] = '\0';
                k_strlcpy(name, path + last_slash + 1, FS_NAME_MAX);
                parent = fs_find_node(parent_path);
            }

            if (!parent)
            {
                return NULL;
            }

            node = fs_create_file(parent, name);
            if (!node)
            {
                return NULL;
            }
        }
        else
        {
            if (node->type != FS_FILE)
            {
                return NULL;
            }
            // Truncate existing file size
            if (fs_write_file(node, NULL, 0) != 0)
            {
                return NULL;
            }
        }
    }
    else if (k_strcmp(mode, "r") == 0)
    {
        if (!node || node->type != FS_FILE)
        {
            return NULL;
        }
    }
    else
    {
        return NULL; // Mode not supported
    }

    fs_file_t *file = (fs_file_t *)kmalloc(sizeof(fs_file_t));
    if (!file)
    {
        return NULL;
    }

    file->node    = node;
    file->offset  = 0;
    file->writing = writing;

    return file;
}

size_t fs_read(fs_file_t *file, void *buf, size_t count)
{
    if (!file || !buf || file->writing)
    {
        return 0;
    }

    if (file->offset >= file->node->size)
    {
        return 0;
    }

    size_t to_read = count;
    if (file->offset + to_read > file->node->size)
    {
        to_read = file->node->size - file->offset;
    }

    if (to_read > 0 && file->node->data)
    {
        k_memcpy(buf, file->node->data + file->offset, to_read);
        file->offset += to_read;
    }

    return to_read;
}

size_t fs_write(fs_file_t *file, const void *buf, size_t count)
{
    if (!file || !buf || !file->writing)
    {
        return 0;
    }

    size_t new_size = file->offset + count;

    char *new_data = (char *)kmalloc(new_size + 1);
    if (!new_data)
    {
        return 0;
    }

    if (file->node->data && file->offset > 0)
    {
        k_memcpy(new_data, file->node->data, file->offset);
    }

    k_memcpy(new_data + file->offset, buf, count);
    new_data[new_size] = '\0';

    if (file->node->data)
    {
        kfree(file->node->data);
    }

    file->node->data = new_data;
    file->node->size = new_size;
    file->offset     = new_size;

    sync_node_to_disk(file->node);

    return count;
}

void fs_fclose(fs_file_t *file)
{
    if (file)
    {
        if (file->writing && file->node)
        {
            sync_node_to_disk(file->node);
        }
        kfree(file);
    }
}