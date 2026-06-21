// vfs.h
//
// Virtual filesystem interface

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fs.h"

#define MAX_FS_NAME 16

typedef struct vfs_ops
{
    int (*open)(struct vfs_node *node, const char *mode);
    size_t (*read)(struct vfs_node *node, uint64_t offset, size_t size, uint8_t *buffer);
    size_t (*write)(struct vfs_node *node, uint64_t offset, size_t size, uint8_t *buffer);
    int (*close)(struct vfs_node *node);
} vfs_ops_t;

typedef struct vfs_node {
    char name[MAX_FS_NAME];
    uint64_t size;
    uint64_t start_block;
    uint64_t end_block;
    void *private_data;
    bool is_directory;
    bool is_file;
    vfs_ops_t *ops;
} vfs_node_t;


#define MAX_MOUNT_POINTS 16

typedef struct mount_point
{
    char device_name[MAX_FS_NAME];
    char mount_path[MAX_FS_NAME];
    vfs_node_t *root;
    fs_node_t *fs;
    struct mount_point *next;
} mount_point_t;

void vfs_init();
void vfs_mount(const char *device_name, const char *mount_path, vfs_node_t *root, fs_node_t *fs);
void vfs_unmount(const char *mount_path);
vfs_node_t *vfs_find_node(const char *path);
vfs_node_t *vfs_get_root();
vfs_node_t *vfs_get_cwd();
void vfs_set_cwd(vfs_node_t *node);

#endif // VFS_H