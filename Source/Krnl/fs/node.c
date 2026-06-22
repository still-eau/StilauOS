// node.c
//
// Generic file system node implementation

#include "node.h"
#include "fs_types.h"
#include "../mem/krnl_mm.h"

static size_t fs_nodes_allocated = 0;
static size_t fs_nodes_freed = 0;

static inline void strncpy(char *dest, const char *src, size_t n)
{
    for (size_t i = 0; i < n && src[i]; i++)
    {
        dest[i] = src[i];
    }
    dest[n - 1] = '\0';
}

static inline int strncmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n && s1[i] && s2[i]; i++)
    {
        if (s1[i] != s2[i])
        {
            return s1[i] - s2[i];
        }
    }
    return s1[n - 1] - s2[n - 1];
}

// Create a new node
fs_node_t *fs_node_create(fs_node_type_t type, const char *name) {
    fs_node_t *node = (fs_node_t *)kmalloc(sizeof(fs_node_t));

    if (!node)
    {
        return NULL;
    }

    node->type = type;

    if (name && *name)
    {
        strncpy(node->name, name, MAX_NAME_LENGTH);
        node->name[MAX_NAME_LENGTH] = '\0';
    }
    else
    {
        node->name[0] = '\0';
    }

    node->parent = NULL;
    node->first_child = NULL;
    node->next_sibling = NULL;
    node->private_data = NULL;

    fs_nodes_allocated++;

    return node;
}

// Destroy a node and all its children recursively
void fs_node_destroy(fs_node_t *node)
{
    if (!node)
    {
        return;
    }

    // Recursively destroy children
    if (node->first_child)
    {
        fs_node_t *current = node->first_child;
        fs_node_t *next;

        while (current)
        {
            next = current->next_sibling;
            fs_node_destroy(current);
            current = next;
        }
    }

    fs_nodes_freed++;

    kfree(node);
}

int fs_node_add_child(fs_node_t *parent, fs_node_t *child)
{
    if (!parent || !child || fs_node_is_file(parent))
    {
        return -1;
    }

    // Update child's parent pointer
    child->parent = parent;

    // Add to the beginning of the children list
    child->next_sibling = parent->first_child;
    parent->first_child = child;

    // Update directory's node count
    if (fs_node_is_dir(parent) && parent->private_data)
    {
        fs_node_t *dir_data = (fs_node_t *)parent->private_data;
        dir_data->inode++;
    }

    return 0;
}

// Remove a child from a directory
int fs_node_remove_child(fs_node_t *parent, fs_node_t *child)
{
    if (!parent || !child || fs_node_is_file(parent))
    {
        return -1;
    }

    // Remove from the children list
    if (parent->first_child == child)
    {
        parent->first_child = child->next_sibling;
    }
    else
    {
        fs_node_t *current = parent->first_child;
        while (current && current->next_sibling != child)
        {
            current = current->next_sibling;
        }
        if (current)
        {
            current->next_sibling = child->next_sibling;
        }
    }

    // Clear child's parent pointer
    child->parent = NULL;

    // Update directory's node count
    if (fs_node_is_dir(parent) && parent->private_data)
    {
        fs_node_t *dir_data = (fs_node_t *)parent->private_data;
        dir_data->inode--;
    }

    return 0;
}

// Search for a child by name
fs_node_t *fs_node_find_child(fs_node_t *parent, const char *name)
{
    if (!parent || !name)
    {
        return NULL;
    }

    // Search in the children list
    fs_node_t *current = parent->first_child;
    while (current)
    {
        if (current->name[0] && strncmp(current->name, name, MAX_NAME_LENGTH) == 0)
        {
            return current;
        }
        current = current->next_sibling;
    }

    return NULL;
}

// Check if a node is a directory
int fs_node_is_dir(const fs_node_t *node)
{
    if (node && node->type == FS_DIRECTORY)
    {
        return 1;
    }

    return 0;
}

// Check if a node is a file
int fs_node_is_file(const fs_node_t *node)
{
    if (node && node->type == FS_FILE)
    {
        return 1;
    }

    return 0;
}