#ifndef FS_PATH_H
#define FS_PATH_H

#include <stddef.h>
#include <stdint.h>
#include "fs_types.h"
#include "node.h"

// Normalize a path string:
// - removes duplicate '/'
// - removes trailing '/'
void path_normalize(char *out, const char *in);

// Split a path into components.
// Example:
// "/home/user/file"
// ->
// ["home", "user", "file"]
//
// Returns number of components.
// NOTE: 'parts' is an array of pointers into 'path_copy'.
size_t path_split(char *path_copy, char **parts, size_t max_parts);

// Resolve a path starting from a given root node.
// Example:
// fs_lookup(root, "/home/user")
// Returns:
//   pointer to fs_node_t if found
//   NULL otherwise
fs_node_t *fs_lookup(fs_node_t *root, const char *path);

// Get parent directory of a path.
// "/home/user/file" -> "/home/user"
void path_get_parent(char *out, const char *path, size_t out_size);

// Get basename of a path.
// "/home/user/file" -> "file"
const char *path_basename(const char *path);

#endif // FS_PATH_H