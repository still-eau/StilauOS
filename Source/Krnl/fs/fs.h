// fs.h - StilauFS custom in-memory file system

#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Maximum length (including null terminator) for a node name.
#define FS_NAME_MAX 128

// Maximum length (including null terminator) for a path passed to fs_find_node.
#define FS_PATH_MAX 256

typedef enum {
    FS_FILE,
    FS_DIRECTORY
} fs_node_type_t;

typedef struct fs_node {
    char name[FS_NAME_MAX];       // Node name (null-terminated)
    fs_node_type_t type;          // Node type (file or directory)
    size_t size;                  // Size in bytes
    union {
        char *data;               // Dynamic data buffer (for files)
        struct {
            struct fs_node *head; // Head of children list (for directories)
        } dir;
    };
    struct fs_node *parent;       // Parent directory node
    struct fs_node *next;         // Sibling node (linked list of entries in same dir)
    uint32_t inode_idx;           // Corresponding disk inode index
} fs_node_t;

// Directory entry returned by fs_readdir.
typedef struct dirent
{
    char     name[FS_NAME_MAX];
    uint32_t type;
    uint32_t size;
} dirent_t;

// Iteration cursor for reading a directory's contents without mutating
// the tree itself. Opaque to callers: only fs.c looks inside it.
typedef struct fs_dir
{
    fs_node_t *dir_node; // the directory being iterated
    fs_node_t *cursor;   // next child to return
} fs_dir_t;

// API
void       fs_init(void);
fs_node_t *fs_get_root(void);
fs_node_t *fs_get_cwd(void);
void       fs_set_cwd(fs_node_t *node);

fs_node_t *fs_create_file(fs_node_t *parent, const char *name);
fs_node_t *fs_create_dir(fs_node_t *parent, const char *name);
int        fs_write_file(fs_node_t *node, const char *data, size_t size);
fs_node_t *fs_find_node(const char *path);
int        fs_remove_node(fs_node_t *node);

// Directory iteration: fs_opendir allocates and returns a cursor,
// fs_readdir advances it (read-only with respect to the tree),
// fs_closedir frees the cursor.
fs_dir_t  *fs_opendir(const char *path);
dirent_t  *fs_readdir(fs_dir_t *dirp, dirent_t *entry_out);
void       fs_closedir(fs_dir_t *dirp);

// Simple sequential file handle, modeled loosely on POSIX FILE*.
// Only "r" (read) and "w" (truncate+write) modes are supported, since
// StilauFS files are flat in-memory buffers, not byte streams on disk.
typedef struct fs_file
{
    fs_node_t *node;    // underlying file node
    size_t     offset;  // current read/write offset into node->data
    int        writing; // 1 if opened for writing, 0 if read-only
} fs_file_t;

fs_file_t *fs_fopen(const char *path, const char *mode);
size_t     fs_read(fs_file_t *file, void *buf, size_t count);
size_t     fs_write(fs_file_t *file, const void *buf, size_t count);
void       fs_fclose(fs_file_t *file);

#endif // FS_H