// fs_types.h
// 
// File system data structures

#include <stdint.h>
#include <stddef.h>

#ifndef FS_TYPES_H
#define FS_TYPES_H

#define MAX_NAME_LENGTH 32
#define MAX_PATH_LENGTH 256
#define STILAUFS_MAX_BLOCKS 134217728

typedef enum fs_node_type {
    FS_FILE = 0,
    FS_DIRECTORY = 1
} fs_node_type_t;

typedef struct fs_node {
    uint64_t inode;

    fs_node_type_t type;

    char name[MAX_NAME_LENGTH + 1];

    struct fs_node *parent;
    struct fs_node *first_child;
    struct fs_node *next_sibling;

    void *private_data;
} fs_node_t;

typedef struct fs_file {
    fs_node_t *node;

    uint64_t offset;
    uint32_t flags;
} fs_file_t;

typedef struct fs_superblock {
    uint32_t magic;
    uint32_t version;
    uint64_t root_inode;
    uint32_t max_nodes;
} fs_superblock_t;

typedef struct {
    fs_superblock_t disk;

    fs_node_t *root;
    uint32_t mounted;
} fs_mount_t;

#endif // FS_TYPES_H