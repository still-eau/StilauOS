// file.c
//
// File implementation for StilauFS

#include "file.h"
#include "fs_types.h"
#include "node.h"
#include "path.h"
#include "../mem/krnl_mm.h"

// Open a file from a node
fs_file_t *fs_file_open(fs_node_t *node, uint32_t flags)
{
    if (!node || fs_node_is_dir(node))
    {
        return NULL;
    }

    fs_file_t *file = (fs_file_t *)kmalloc(sizeof(fs_file_t));
    if (!file)
    {
        return NULL;
    }

    file->node = node;
    file->offset = 0;
    file->flags = flags;

    return file;
}

// Open by path
fs_file_t *fs_file_open_path(const char *path, uint32_t flags)
{
    fs_node_t *node = fs_lookup(NULL, path);
    if (!node)
    {
        return NULL;
    }
    return fs_file_open(node, flags);
}

// Read from file
size_t fs_file_read(fs_file_t *file, void *buffer, size_t size)
{
    if (!file || !buffer || size == 0)
    {
        return 0;
    }

    // Read from disk
    
    return 0;
}

// Write to file
size_t fs_file_write(fs_file_t *file, const void *buffer, size_t size)
{
    if (!file || !buffer || size == 0)
    {
        return 0;
    }

    // Write to disk
    
    return 0;
}

// Seek inside file
size_t fs_file_seek(fs_file_t *file, size_t offset)
{
    if (!file)
    {
        return 0;
    }

    if (offset < 0)
    {
        return 0;
    }

    file->offset = offset;
    return offset;
}

// Close file handle
void fs_file_close(fs_file_t *file)
{
    if (!file)
    {
        return;
    }

    kfree(file);
}

// Get underlying node
fs_node_t *fs_file_node(fs_file_t *file)
{
    if (!file)
    {
        return NULL;
    }

    return file->node;
}

// Get current offset
size_t fs_file_tell(fs_file_t *file)
{
    if (!file)
    {
        return 0;
    }

    return file->offset;
}

// Get file size
size_t fs_file_size(fs_file_t *file)
{
    if (!file)
    {
        return 0;
    }

    return file->node->inode;
}