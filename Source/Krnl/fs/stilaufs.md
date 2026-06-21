# StilauFS - Specifications & Operation

StilauFS is the personalized, persistent, and block-indexed file system designed for StilauOS. This document describes in detail its organization on disk and its operation.

---

## 1. Disk Organization (Sectors Layout)

The virtual disk of StilauOS (`os.img`) is a raw image of 2 MB (4096 sectors of 512 bytes). The physical organization of the sectors is as follows :

| Sector Range | Size | Role |
| :--- | :--- | :--- |
| **Secteur 0** | 1 secteur (512 o) | Bootloader |
| **Secteurs 1 - 127** | 127 secteurs (~63 Ko) | Kernel |
| **Secteurs 128 - 255** | 128 secteurs (64 Ko) | Reserved / Unused |
| **Secteur 256** | 1 secteur (512 o) | **StilauFS Superblock** |
| **Secteurs 257 - 288** | 32 secteurs (16 Ko) | **Inode Table (128 Inodes)** |
| **Secteur 289** | 1 secteur (512 o) | **Inode Bitmap (4096 entries max)** |
| **Secteur 290** | 1 secteur (512 o) | **Data Bitmap (4096 entries max)** |
| **Secteurs 291 - 4095** | 3805 secteurs (~1.86 Mo) | **Data Blocks** |

---

## 2. Disk Structures

### A. The Superbloc (`stilaufs_superblock_t`)
Located at sector 256, it contains the global metadata of the file system:
* **magic** (`0x53544C46` / `"STLF"`) : Signature of StilauFS.
* **total_sectors** : Number of sectors allocated to StilauFS.
* **num_inodes** : Maximum number of inodes (fixed to 128).
* **num_data_blocks** : Number of user data blocks.
* **inode_table_lba** : LBA of the start of the inode table (257).
* **inode_bitmap_lba** : LBA of the inode bitmap (289).
* **data_bitmap_lba** : LBA of the data bitmap (290).
* **data_blocks_lba** : LBA of the start of the data blocks (291).

### B. The Inode (`stilaufs_disk_inode_t`)
Each inode represents a file or a directory. It is exactly 128 bytes, allowing 4 to be stored per sector :
* **size** (4 octets) : Size in bytes of the file.
* **type** (2 octets) : `FS_FILE` (0) or `FS_DIRECTORY` (1).
* **used** (2 octets) : Flag indicating if the inode is active (1) or free (0).
* **direct** (52 octets) : Array of 13 direct block pointers. Each entry contains the index of a data block. With 512-byte blocks, the maximum file size is `13 * 512 = 6.5 KB`.
* **parent_inode** (4 octets) : The index of the parent inode (allows to structure the hierarchy in a simple way).
* **name** (60 octets) : Name of the element (null-terminated).
* **padding** (4 octets) : Structure alignment.

---

## 3. Persistence Strategy

### A. Initial Formatting and Detection
Lors de l'initialisation de l'OS (`fs_init`), le noyau lit le secteur 256 :
1. **Valid Magic** : The global structures (bitmaps and inode table) are loaded into RAM, then the in-memory tree (`fs_node_t`) is recursively reconstructed from the inode table (Mounting process).
2. **Invalid Magic** : The disk is considered empty. The system formats the partition by writing the initial tables (Superblock, Inode 0 for the root `/`) and generating the system directories (`/docs`, `/bin`, `/dev`) as well as the welcome file.

### B. Write-Through Synchronization
To avoid data corruption in case of QEMU shutdown, any file modification or directory creation uses the **write-through** principle:
* Files/directories are created/modified in RAM for fast access to the in-memory tree.
* Simultanément, la table d'inodes, les bitmaps et les secteurs de données correspondants sont réécrits sur le disque via des instructions I/O de ports ATA.

---

## 4. ATA PIO Driver (Port I/O)
The virtual disk is accessed using the primary ATA storage controller in PIO (Polling) mode:
* **Sector Read**: Command `0x20` sent on port `0x1F7` after LBA selection, wait for DRQ (Data Request) signal, then read 256 words (16-bit) from data port `0x1F0`.
* **Sector Write**: Command `0x30`, wait for DRQ, write 256 words via port `0x1F0` followed by ATA Cache Flush command (`0xE7`).
