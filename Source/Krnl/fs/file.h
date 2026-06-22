#ifndef FS_FILE_H
#define FS_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "fs_types.h"
#include "node.h"

#define FS_O_RDONLY 0x01
#define FS_O_WRONLY 0x02
#define FS_O_RDWR   0x04
#define FS_O_APPEND 0x08

typedef struct fs_file fs_file_t;

// Open a file from a node.
// This does NOT access disk directly.
// It just creates an "open file handle".
fs_file_t *fs_file_open(fs_node_t *node, uint32_t flags);

// Open by path (uses path.c / fs_lookup).
fs_file_t *fs_file_open_path(const char *path, uint32_t flags);

// Read from file. Returns number of bytes read.
size_t fs_file_read(fs_file_t *file, void *buffer, size_t size);

// Write to file. Returns number of bytes written.
size_t fs_file_write(fs_file_t *file, const void *buffer, size_t size);

// Seek inside file. Returns new offset.
size_t fs_file_seek(fs_file_t *file, size_t offset);

// Close file handle.
void fs_file_close(fs_file_t *file);

// Get underlying node.
fs_node_t *fs_file_node(fs_file_t *file);

// Get current offset & Returns new offset.
size_t fs_file_tell(fs_file_t *file);

#endif // FS_FILE_H