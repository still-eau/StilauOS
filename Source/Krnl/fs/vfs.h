#ifndef FS_VFS_H
#define FS_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "fs_types.h"
#include "node.h"
#include "path.h"
#include "file.h"

// Directory entry structure for fs_readdir
typedef struct {
    char name[MAX_NAME_LENGTH + 1];
    uint32_t type;
    uint32_t size;
} dirent_t;

// Directory handle structure for fs_opendir/fs_readdir
typedef struct {
    fs_node_t *node;       // The directory node
    fs_node_t *current;    // The current child node being iterated
} fs_dir_t;

// VFS Initialization and Device Setup
int fs_init(void);

// Global state getters/setters
fs_node_t *fs_get_root(void);
fs_node_t *fs_get_cwd(void);
void fs_set_cwd(fs_node_t *node);
fs_node_t *fs_find_node(const char *path);

// VFS File Interface
fs_file_t *fs_fopen(const char *path, const char *mode);
size_t fs_read(fs_file_t *file, void *buffer, size_t size);
size_t fs_write(fs_file_t *file, const void *buffer, size_t size);
void fs_fclose(fs_file_t *file);

// VFS Directory Interface
fs_dir_t *fs_opendir(const char *path);
int fs_readdir(fs_dir_t *dir, dirent_t *entry);
void fs_closedir(fs_dir_t *dir);

// VFS Node Creation/Removal
int fs_create_file(fs_node_t *parent, const char *name);
int fs_create_dir(fs_node_t *parent, const char *name);
int fs_remove_node(fs_node_t *node);

#endif // FS_VFS_H
