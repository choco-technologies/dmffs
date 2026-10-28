#ifndef DMFFS_TREE_H
#define DMFFS_TREE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * The file system of a writable mount, in RAM: directories and files with their data. It is loaded from an image
 * of TLV entries (the format make_dmffs writes) and written back as one - serialize / load are the same code a
 * boot loader needs, so this file depends on nothing of DMOD but the allocator (dmffs_port.h). Errors are negative
 * errno values; the DMFSI layer (dmffs_rw.c) maps them.
 */

#define DMFFS_NAME_MAX      255u    /* A name is 1 .. 255 bytes, no '/' */
#define DMFFS_DEPTH_MAX     8u      /* Directories below the root */

typedef struct dmffs_node dmffs_node_t;

struct dmffs_node
{
    char*           name;           /* NULL for the root */
    dmffs_node_t*   parent;
    dmffs_node_t**  children;       /* A directory's entries, in the order they were added */
    uint32_t        child_count;
    uint32_t        child_capacity;
    uint8_t*        data;           /* A file's content */
    size_t          size;
    size_t          capacity;
    uint32_t        attr;           /* DMFSI_ATTR_* */
    uint32_t        mtime;
    uint32_t        opens;          /* Handles that keep the node: it cannot be removed or moved meanwhile */
    bool            is_dir;
};

dmffs_node_t* dmffs_tree_create(void);                      /* The root, an empty directory */
void dmffs_tree_destroy(dmffs_node_t* root);                /* (safe with NULL) */

dmffs_node_t* dmffs_node_find_child(const dmffs_node_t* dir, const char* name);

/* -EEXIST, -EINVAL (a bad name, too deep), -ENOMEM */
int dmffs_tree_add(dmffs_node_t* dir, const char* name, bool is_dir, uint32_t attr, dmffs_node_t** node);

/* -EBUSY (open), -ENOTEMPTY, -EINVAL (the root) */
int dmffs_tree_remove(dmffs_node_t* node);

/* Into another (or the same) directory, under another name; an existing file there is replaced (-EEXIST for a directory) */
int dmffs_tree_move(dmffs_node_t* node, dmffs_node_t* dir, const char* name);

/* The path "a/b/c" (a leading '/' is fine) resolved from the root. */
typedef struct
{
    dmffs_node_t*   node;       /* The entry, NULL when only the last component is missing */
    dmffs_node_t*   parent;     /* The directory it is (or would be) in; NULL for the root */
    const char*     leaf;       /* Its name; NULL for the root. Valid until release. */
    char*           buffer;
} dmffs_lookup_t;

/* 0; -ENOENT (a directory on the way is missing), -ENOTDIR, -EINVAL ("..", an empty or too long name), -ENOMEM */
int dmffs_tree_lookup(dmffs_node_t* root, const char* path, dmffs_lookup_t* lookup);
void dmffs_lookup_release(dmffs_lookup_t* lookup);

/* File content */
size_t dmffs_file_read(const dmffs_node_t* file, size_t offset, void* buffer, size_t size);
int dmffs_file_write(dmffs_node_t* file, size_t offset, const void* buffer, size_t size);     /* grows it; -ENOMEM */
int dmffs_file_truncate(dmffs_node_t* file, size_t size);

/* The image: how many bytes, writing it, reading it */
size_t dmffs_tree_image_size(const dmffs_node_t* root);
int dmffs_tree_serialize(const dmffs_node_t* root, uint8_t* out, size_t capacity, size_t* written);   /* -ENOSPC */

/*
 * Loads an image into an empty root. 0 - loaded; 1 - blank (erased flash: an empty file system); -EBADMSG - it is not
 * a file system image (a broken structure, a CRC that does not match); -ENOMEM
 */
int dmffs_tree_load(dmffs_node_t* root, const uint8_t* image, size_t size);

uint32_t dmffs_crc32(const uint8_t* data, size_t size);

#endif // DMFFS_TREE_H
