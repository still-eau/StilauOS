// path.c
//
// Path manipulation utilities

#include "path.h"
#include "node.h"

// Helper: copy string safely
static inline void path_strncpy(char *dest, const char *src, size_t n)
{
    for (size_t i = 0; i < n && src[i]; i++)
    {
        dest[i] = src[i];
    }
    dest[n - 1] = '\0';
}

// Helper: strnchr
static inline char *path_strnchr(const char *s, int c, size_t n)
{
    for (size_t i = 0; i < n && s[i]; i++)
    {
        if (s[i] == c)
        {
            return (char *)&s[i];
        }
    }
    return NULL;
}

// Normalize a path string
void path_normalize(char *out, const char *in)
{
    if (!in || !out)
    {
        return;
    }

    const char *src = in;
    char *dst = out;

    // Copy while skipping duplicate slashes
    int last_was_slash = 0;
    while (*src)
    {
        if (*src == '/')
        {
            if (!last_was_slash)
            {
                *dst++ = '/';
                last_was_slash = 1;
            }
        }
        else
        {
            *dst++ = *src;
            last_was_slash = 0;
        }
        src++;
    }

    // Remove trailing slash unless root
    if (dst > out + 1 && *(dst - 1) == '/')
    {
        dst--;
    }

    *dst = '\0';
}

// Split a path into components
// Returns number of components
size_t path_split(char *path_copy, char **parts, size_t max_parts)
{
    if (!path_copy || !parts || max_parts == 0)
    {
        return 0;
    }

    size_t count = 0;

    // Skip leading slash
    char *p = path_copy;
    if (*p == '/')
    {
        p++;
    }

    while (*p && count < max_parts)
    {
        char *start = p;
        // Find next slash or end of string
        while (*p && *p != '/')
        {
            p++;
        }

        if (p - start > 0)
        {
            parts[count++] = start;

            if (*p == '/')
            {
                *p = '\0';
                p++;
            }
        }
        else
        {
            p++;
        }
    }

    return count;
}

// Resolve a path starting from a root node
fs_node_t *fs_lookup(fs_node_t *root, const char *path)
{
    if (!root || !path || !*path)
    {
        return NULL;
    }

    // Handle root-only path
    if (path[0] == '/' && path[1] == '\0')
    {
        return root;
    }

    // Copy and normalize the path
    char path_copy[MAX_PATH_LENGTH + 1];
    path_normalize(path_copy, path);

    // If path is empty after normalization
    if (path_copy[0] == '\0')
    {
        return NULL;
    }

    char *parts[16]; // Max 16 components, extend if needed
    size_t num_parts = path_split(path_copy, parts, 16);

    if (num_parts == 0)
    {
        return NULL;
    }

    fs_node_t *current = root;

    for (size_t i = 0; i < num_parts; i++)
    {
        if (!current || !fs_node_is_dir(current))
        {
            return NULL;
        }

        fs_node_t *child = fs_node_find_child(current, parts[i]);
        if (!child)
        {
            return NULL;
        }

        current = child;
    }

    return current;
}

// Get parent directory of a path
void path_get_parent(char *out, const char *path, size_t out_size)
{
    if (!path || !out)
    {
        return;
    }

    // Find the last slash
    const char *last_slash = path_strnchr(path, '/', MAX_PATH_LENGTH);

    if (!last_slash || last_slash == path)
    {
        // Root directory or single component
        if (out_size > 0)
        {
            out[0] = '/';
            out[1] = '\0';
        }
    }
    else
    {
        size_t len = last_slash - path;
        if (len >= out_size)
        {
            len = out_size - 1;
        }

        path_strncpy(out, path, len);

        if (out_size > 0)
        {
            out[len] = '\0';
        }

        // Remove trailing slash if any
        if (len > 1 && out[len - 1] == '/')
        {
            out[len - 1] = '\0';
        }
    }
}

// Get basename of a path
const char *path_basename(const char *path)
{
    if (!path || !*path)
    {
        return NULL;
    }

    // Find last slash
    const char *last_slash = path_strnchr(path, '/', MAX_PATH_LENGTH);

    if (!last_slash || last_slash[1] == '\0')
    {
        return path;
    }

    return last_slash + 1;
}