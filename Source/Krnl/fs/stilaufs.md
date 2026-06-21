# StilauFS - Spécification et Fonctionnement

StilauFS est le système de fichiers personnalisé, persistant et indexé par blocs conçu pour StilauOS. Ce document décrit en détail son organisation sur le disque et son fonctionnement.

---

## 1. Organisation du Disque (Sectors Layout)

Le disque virtuel de StilauOS (`os.img`) est une image brute de 2 Mo (4096 secteurs de 512 octets). L'organisation physique des secteurs est la suivante :

| Plage de Secteurs | Taille | Rôle |
| :--- | :--- | :--- |
| **Secteur 0** | 1 secteur (512 o) | Secteur d'amorçage (Bootloader) |
| **Secteurs 1 - 127** | 127 secteurs (~63 Ko) | Emplacement binaire du Kernel |
| **Secteurs 128 - 255** | 128 secteurs (64 Ko) | Espace réservé / inutilisé |
| **Secteur 256** | 1 secteur (512 o) | **Superbloc StilauFS** |
| **Secteurs 257 - 288** | 32 secteurs (16 Ko) | **Table d'Inodes (128 Inodes)** |
| **Secteur 289** | 1 secteur (512 o) | **Bitmap d'Inodes (4096 entrées max)** |
| **Secteur 290** | 1 secteur (512 o) | **Bitmap de Blocs de Données (4096 entrées max)** |
| **Secteurs 291 - 4095** | 3805 secteurs (~1.86 Mo) | **Blocs de Données (Data Blocks)** |

---

## 2. Structures de Données sur Disque

### A. Le Superbloc (`stilaufs_superblock_t`)
Situé au secteur 256, il contient les métadonnées globales du système de fichiers :
* **magic** (`0x53544C46` / `"STLF"`) : Signature de StilauFS.
* **total_sectors** : Nombre de secteurs alloués à StilauFS.
* **num_inodes** : Nombre maximal d'inodes (fixé à 128).
* **num_data_blocks** : Nombre de blocs de données utilisateur.
* **inode_table_lba** : LBA de début de la table d'inodes (257).
* **inode_bitmap_lba** : LBA du bitmap d'inodes (289).
* **data_bitmap_lba** : LBA du bitmap de blocs de données (290).
* **data_blocks_lba** : LBA de début des blocs de données (291).

### B. L'Inode (`stilaufs_disk_inode_t`)
Chaque inode représente un fichier ou un dossier. Il fait exactement 128 octets, permettant d'en stocker 4 par secteur :
* **size** (4 octets) : Taille en octets du fichier.
* **type** (2 octets) : `FS_FILE` (0) ou `FS_DIRECTORY` (1).
* **used** (2 octets) : Drapeau indiquant si l'inode est actif (1) ou libre (0).
* **direct** (52 octets) : Tableau de 13 pointeurs de blocs directs. Chaque entrée contient l'index d'un bloc de données. Avec des blocs de 512 octets, la taille maximale par fichier est de `13 * 512 = 6,5 Ko`.
* **parent_inode** (4 octets) : L'index de l'inode parent (permet de structurer la hiérarchie de manière simple).
* **name** (60 octets) : Nom de l'élément (null-terminé).
* **padding** (4 octets) : Alignement de la structure.

---

## 3. Stratégie de Persistance

### A. Formatage initial et Détection
Lors de l'initialisation de l'OS (`fs_init`), le noyau lit le secteur 256 :
1. **Magic Valide** : Les structures globales (bitmaps et table d'inodes) sont chargées en mémoire vive, puis l'arborescence in-memory (`fs_node_t`) est reconstruite récursivement à partir de la table d'inodes (Processus de montage).
2. **Magic Invalide** : Le disque est considéré comme vierge. Le système formate la partition en écrivant les tables initiales (Superbloc, Inode 0 pour la racine `/`) et en générant les dossiers systèmes (`/docs`, `/bin`, `/dev`) ainsi que le fichier de bienvenue.

### B. Synchronisation à l'Écriture (Write-Through)
Pour éviter la corruption des données en cas d'extinction brutale de QEMU, toute modification de fichier ou création de répertoire utilise le principe du **write-through** :
* Les fichiers/dossiers sont créés/modifiés en RAM pour la rapidité d'accès de l'arborescence in-memory.
* Simultanément, la table d'inodes, les bitmaps et les secteurs de données correspondants sont réécrits sur le disque via des instructions I/O de ports ATA.

---

## 4. Pilote ATA PIO (Port I/O)
Le système accède au disque virtuel en utilisant le contrôleur de stockage ATA primaire en mode PIO (Polling) :
* **Lecture de Secteur** : Commande `0x20` envoyée sur le port `0x1F7` après sélection du LBA, attente du signal DRQ (Data Request) puis lecture de 256 mots (16-bit) sur le port de données `0x1F0`.
* **Écriture de Secteur** : Commande `0x30`, attente de DRQ, écriture de 256 mots via le port `0x1F0` suivi de la commande ATA Cache Flush (`0xE7`).
