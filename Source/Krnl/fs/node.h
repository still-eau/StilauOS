// node.h
//
// Generic file system node interface.

#ifndef FS_NODE_H
#define FS_NODE_H

#include <stdint.h>
#include "fs_types.h"

typedef struct fs_node fs_node_t;

// Create a new node
// Returns NULL on allocation failure.
fs_node_t *fs_node_create(fs_node_type_t type, const char *name);

// Destroy a node and all its children.
void fs_node_destroy(fs_node_t *node);

// Add a child node to a directory.
// Returns:
//   0  success
//  -1  invalid arguments
int fs_node_add_child(fs_node_t *parent, fs_node_t *child);

// Remove a child from a directory.
// Returns:
//   0  success
//  -1  child not found
int fs_node_remove_child(fs_node_t *parent, fs_node_t *child);

// Search for a child by name.
// Returns NULL if not found.
fs_node_t *fs_node_find_child(fs_node_t *parent, const char *name);

// Returns non-zero if node is a directory.
int fs_node_is_dir(const fs_node_t *node);

// Returns non-zero if node is a file.
int fs_node_is_file(const fs_node_t *node);

#endif // FS_NODE_H