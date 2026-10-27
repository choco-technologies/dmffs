#include "dmod.h"
#include "dmosi.h"
#include "dmfsi.h"
#include "dmffs_rw.h"
#include "dmffs_tree.h"
#include "dmffs_medium.h"
#include <errno.h>
#include <string.h>

#define MAGIC_RW        0x44464652u     /* 'DFFR' */
#define MAGIC_FILE      0x44464648u     /* 'DFFH' */
#define MAGIC_DIR       0x44464644u     /* 'DFFD' */
#define ATTR_USER_MASK  0x37u           /* READONLY | HIDDEN | SYSTEM | ARCHIVE (DIRECTORY is the node's kind) */

struct dmffs_rw
{
    uint32_t        magic;
    dmosi_mutex_t   lock;
    dmffs_medium_t* medium;
    dmffs_node_t*   root;
    bool            dirty;
};

typedef struct
{
    uint32_t        magic;
    dmffs_node_t*   node;
    dmfsi_offset_t  position;
    bool            readable, writable, append;
} file_t;

typedef struct
{
    uint32_t        magic;
    dmffs_node_t*   node;
    uint32_t        index;
} dir_t;

static int to_dmfsi(int error)
{
    switch (error)
    {
        case 0:             return DMFSI_OK;
        case -ENOENT:
        case -ENOTDIR:      return DMFSI_ERR_NOT_FOUND;
        case -EEXIST:       return DMFSI_ERR_EXISTS;
        case -ENOSPC:       return DMFSI_ERR_NO_SPACE;
        case -ENOTEMPTY:    return DMFSI_ERR_NOT_EMPTY;
        case -EINVAL:
        case -EROFS:        return DMFSI_ERR_INVALID;
        default:            return DMFSI_ERR_GENERAL;
    }
}

/* ---- Mount ---- */

static int load_image(dmffs_rw_t* rw)
{
    size_t size = dmffs_medium_size(rw->medium);
    uint8_t* image = Dmod_Malloc(size);
    if (image == NULL)
        return -ENOMEM;
    int rc = dmffs_medium_read(rw->medium, image, size);
    if (rc == 0)
        rc = dmffs_tree_load(rw->root, image, size);
    Dmod_Free(image);
    if (rc == -EBADMSG)
    {
        DMOD_LOG_WARN("dmffs: the file system on the device is damaged (a write that was cut short?): starting empty\n");
        rw->dirty = true;               /* (the next flush replaces it) */
        return 0;
    }
    return (rc < 0) ? rc : 0;
}

dmffs_rw_t* dmffs_rw_open(const char* device)
{
    dmffs_rw_t* rw = Dmod_Malloc(sizeof(*rw));
    if (rw == NULL)
        return NULL;
    memset(rw, 0, sizeof(*rw));
    rw->medium = dmffs_medium_open(device);
    rw->root = dmffs_tree_create();
    rw->lock = dmosi_mutex_create(false);
    if (rw->medium == NULL || rw->root == NULL || rw->lock == NULL || load_image(rw) != 0)
    {
        DMOD_LOG_ERROR("dmffs: cannot open the device '%s'\n", device);
        dmffs_tree_destroy(rw->root);
        dmffs_medium_close(rw->medium);
        if (rw->lock != NULL)
            dmosi_mutex_destroy(rw->lock);
        Dmod_Free(rw);
        return NULL;
    }
    rw->magic = MAGIC_RW;
    return rw;
}

/* ---- Writing the medium ---- */

static int commit_locked(dmffs_rw_t* rw)
{
    if (!rw->dirty)
        return DMFSI_OK;
    size_t size = dmffs_tree_image_size(rw->root);
    if (size > dmffs_medium_size(rw->medium))
        return DMFSI_ERR_NO_SPACE;
    uint8_t* image = Dmod_Malloc(size);
    if (image == NULL)
        return DMFSI_ERR_GENERAL;
    int rc = dmffs_tree_serialize(rw->root, image, size, NULL);
    if (rc == 0)
        rc = dmffs_medium_rewrite(rw->medium, image, size);
    Dmod_Free(image);
    if (rc == 0)
        rw->dirty = false;
    else
        DMOD_LOG_ERROR("dmffs: writing the device failed (%d); the changes stay in RAM\n", rc);
    return to_dmfsi(rc);
}

int dmffs_rw_commit(dmffs_rw_t* rw)
{
    if (rw == NULL || rw->magic != MAGIC_RW)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    int rc = commit_locked(rw);
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_close(dmffs_rw_t* rw)
{
    if (rw == NULL || rw->magic != MAGIC_RW)
        return DMFSI_ERR_INVALID;
    int rc = dmffs_rw_commit(rw);
    rw->magic = 0;
    dmffs_tree_destroy(rw->root);
    dmffs_medium_close(rw->medium);
    dmosi_mutex_destroy(rw->lock);
    Dmod_Free(rw);
    return rc;
}

/* ---- Helpers ---- */

static bool valid(const dmffs_rw_t* rw)
{
    return rw != NULL && rw->magic == MAGIC_RW;
}

static file_t* as_file(dmffs_rw_t* rw, void* fp)
{
    file_t* f = fp;
    return (valid(rw) && f != NULL && f->magic == MAGIC_FILE) ? f : NULL;
}

/* The space the image would need when a file grows by `grow` bytes */
static bool fits(const dmffs_rw_t* rw, size_t grow)
{
    return dmffs_tree_image_size(rw->root) + grow <= dmffs_medium_size(rw->medium);
}

/* ---- Files ---- */

static int open_new(dmffs_rw_t* rw, dmffs_lookup_t* l, int attr, dmffs_node_t** node)
{
    if (l->parent == NULL || !fits(rw, 8u + 12u + 12u + strlen(l->leaf) + 8u))
        return (l->parent == NULL) ? DMFSI_ERR_INVALID : DMFSI_ERR_NO_SPACE;
    int rc = dmffs_tree_add(l->parent, l->leaf, false, (uint32_t)attr & ATTR_USER_MASK, node);
    if (rc == 0)
        rw->dirty = true;
    return to_dmfsi(rc);
}

static int open_existing(dmffs_rw_t* rw, dmffs_node_t* node, int mode, bool writable)
{
    if (node->is_dir || (writable && (node->attr & DMFSI_ATTR_READONLY)))
        return DMFSI_ERR_INVALID;
    if (writable && (mode & DMFSI_O_TRUNC) && node->size != 0)
    {
        dmffs_file_truncate(node, 0);
        rw->dirty = true;
    }
    return DMFSI_OK;
}

int dmffs_rw_fopen(dmffs_rw_t* rw, void** fp, const char* path, int mode, int attr)
{
    int access = mode & 3;
    bool writable = (access & DMFSI_O_WRONLY) != 0;
    if (!valid(rw) || fp == NULL || path == NULL || (writable && !dmffs_medium_writable(rw->medium)))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = to_dmfsi(dmffs_tree_lookup(rw->root, path, &l));
    if (rc == DMFSI_OK)
    {
        dmffs_node_t* node = l.node;
        if (node == NULL)
            rc = (writable && (mode & DMFSI_O_CREAT)) ? open_new(rw, &l, attr, &node) : DMFSI_ERR_NOT_FOUND;
        else
            rc = open_existing(rw, node, mode, writable);
        file_t* f = (rc == DMFSI_OK) ? Dmod_Malloc(sizeof(*f)) : NULL;
        if (rc == DMFSI_OK && f == NULL)
            rc = DMFSI_ERR_GENERAL;
        if (f != NULL)
        {
            *f = (file_t){ MAGIC_FILE, node, 0, (access & DMFSI_O_RDONLY) != 0, writable, (mode & DMFSI_O_APPEND) != 0 };
            node->opens++;
            *fp = f;
        }
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_fclose(dmffs_rw_t* rw, void* fp)
{
    file_t* f = as_file(rw, fp);
    if (f == NULL)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    f->node->opens--;
    dmosi_mutex_unlock(rw->lock);
    f->magic = 0;
    Dmod_Free(f);
    return DMFSI_OK;
}

int dmffs_rw_fread(dmffs_rw_t* rw, void* fp, void* buffer, size_t size, size_t* read)
{
    file_t* f = as_file(rw, fp);
    if (f == NULL || buffer == NULL || read == NULL || !f->readable)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    *read = dmffs_file_read(f->node, (size_t)f->position, buffer, size);
    f->position += (dmfsi_offset_t)*read;
    dmosi_mutex_unlock(rw->lock);
    return DMFSI_OK;
}

int dmffs_rw_fwrite(dmffs_rw_t* rw, void* fp, const void* buffer, size_t size, size_t* written)
{
    file_t* f = as_file(rw, fp);
    if (written != NULL)
        *written = 0;
    if (f == NULL || (buffer == NULL && size != 0) || !f->writable)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    if (f->append)
        f->position = (dmfsi_offset_t)f->node->size;
    size_t end = (size_t)f->position + size;
    int rc = DMFSI_OK;
    if (end > f->node->size && !fits(rw, end - f->node->size))
        rc = DMFSI_ERR_NO_SPACE;
    else if (size != 0)
        rc = to_dmfsi(dmffs_file_write(f->node, (size_t)f->position, buffer, size));
    if (rc == DMFSI_OK)
    {
        f->position += (dmfsi_offset_t)size;
        rw->dirty = rw->dirty || size != 0;
        if (written != NULL)
            *written = size;
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

dmfsi_offset_t dmffs_rw_lseek(dmffs_rw_t* rw, void* fp, dmfsi_offset_t offset, int whence)
{
    file_t* f = as_file(rw, fp);
    if (f == NULL || (whence != DMFSI_SEEK_SET && whence != DMFSI_SEEK_CUR && whence != DMFSI_SEEK_END))
        return -1;
    dmosi_mutex_lock(rw->lock);
    dmfsi_offset_t base = (whence == DMFSI_SEEK_SET) ? 0 : (whence == DMFSI_SEEK_CUR) ? f->position : (dmfsi_offset_t)f->node->size;
    dmfsi_offset_t position = base + offset;
    position = (position < 0) ? 0 : position;
    if (!f->writable && (size_t)position > f->node->size)
        position = (dmfsi_offset_t)f->node->size;
    f->position = position;
    dmosi_mutex_unlock(rw->lock);
    return position;
}

dmfsi_offset_t dmffs_rw_tell(dmffs_rw_t* rw, void* fp)
{
    file_t* f = as_file(rw, fp);
    return (f != NULL) ? f->position : -1;
}

int dmffs_rw_eof(dmffs_rw_t* rw, void* fp)
{
    file_t* f = as_file(rw, fp);
    return (f == NULL) ? -1 : ((size_t)f->position >= f->node->size ? 1 : 0);
}

dmfsi_size_t dmffs_rw_size(dmffs_rw_t* rw, void* fp)
{
    file_t* f = as_file(rw, fp);
    return (f != NULL) ? f->node->size : 0;
}

int dmffs_rw_getc(dmffs_rw_t* rw, void* fp)
{
    uint8_t c;
    size_t n = 0;
    return (dmffs_rw_fread(rw, fp, &c, 1, &n) == DMFSI_OK && n == 1) ? (int)c : -1;
}

int dmffs_rw_putc(dmffs_rw_t* rw, void* fp, int c)
{
    uint8_t byte = (uint8_t)c;
    size_t n = 0;
    return (dmffs_rw_fwrite(rw, fp, &byte, 1, &n) == DMFSI_OK && n == 1) ? (int)byte : -1;
}

/* ---- Directories ---- */

int dmffs_rw_opendir(dmffs_rw_t* rw, void** dp, const char* path)
{
    if (!valid(rw) || dp == NULL)
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = to_dmfsi(dmffs_tree_lookup(rw->root, path != NULL ? path : "", &l));
    if (rc == DMFSI_OK)
    {
        bool is_dir = l.node != NULL && l.node->is_dir;
        dir_t* d = is_dir ? Dmod_Malloc(sizeof(*d)) : NULL;
        rc = is_dir ? (d != NULL ? DMFSI_OK : DMFSI_ERR_GENERAL) : DMFSI_ERR_NOT_FOUND;
        if (d != NULL)
        {
            *d = (dir_t){ MAGIC_DIR, l.node, 0 };
            l.node->opens++;
            *dp = d;
        }
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_readdir(dmffs_rw_t* rw, void* dp, dmfsi_dir_entry_t* entry)
{
    dir_t* d = dp;
    if (!valid(rw) || d == NULL || d->magic != MAGIC_DIR || entry == NULL)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    int rc = DMFSI_ERR_NOT_FOUND;
    if (d->index < d->node->child_count)
    {
        const dmffs_node_t* child = d->node->children[d->index++];
        strncpy(entry->name, child->name, sizeof(entry->name) - 1u);
        entry->name[sizeof(entry->name) - 1u] = '\0';
        entry->size = child->is_dir ? 0 : child->size;
        entry->attr = child->attr;
        entry->time = child->mtime;
        rc = DMFSI_OK;
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_closedir(dmffs_rw_t* rw, void* dp)
{
    dir_t* d = dp;
    if (!valid(rw) || d == NULL || d->magic != MAGIC_DIR)
        return DMFSI_ERR_INVALID;
    dmosi_mutex_lock(rw->lock);
    d->node->opens--;
    dmosi_mutex_unlock(rw->lock);
    d->magic = 0;
    Dmod_Free(d);
    return DMFSI_OK;
}

/* ---- Names ---- */

/* The node of a path (the lookup stays for the caller to release) or the DMFSI error. Called with the lock held. */
static int find(dmffs_rw_t* rw, const char* path, dmffs_lookup_t* l)
{
    int rc = to_dmfsi(dmffs_tree_lookup(rw->root, path, l));
    if (rc == DMFSI_OK && l->node == NULL)
    {
        dmffs_lookup_release(l);
        return DMFSI_ERR_NOT_FOUND;
    }
    return rc;
}

int dmffs_rw_stat(dmffs_rw_t* rw, const char* path, dmfsi_stat_t* stat)
{
    if (!valid(rw) || path == NULL || stat == NULL)
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = find(rw, path, &l);
    if (rc == DMFSI_OK)
    {
        stat->size = l.node->is_dir ? 0 : l.node->size;
        stat->attr = l.node->attr;
        stat->ctime = stat->mtime = l.node->mtime;
        stat->atime = 0;
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_direxists(dmffs_rw_t* rw, const char* path)
{
    if (!valid(rw) || path == NULL)
        return 0;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int is_dir = 0;
    if (find(rw, path, &l) == DMFSI_OK)
    {
        is_dir = l.node->is_dir ? 1 : 0;
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return is_dir;
}

int dmffs_rw_mkdir(dmffs_rw_t* rw, const char* path, int mode)
{
    if (!valid(rw) || path == NULL || !dmffs_medium_writable(rw->medium))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = to_dmfsi(dmffs_tree_lookup(rw->root, path, &l));
    if (rc == DMFSI_OK)
    {
        if (l.node != NULL)
            rc = DMFSI_ERR_EXISTS;
        else if (l.parent == NULL)
            rc = DMFSI_ERR_INVALID;
        else if (!fits(rw, 40u + strlen(l.leaf)))
            rc = DMFSI_ERR_NO_SPACE;
        else
        {
            rc = to_dmfsi(dmffs_tree_add(l.parent, l.leaf, true, (uint32_t)mode & ATTR_USER_MASK, NULL));
            rw->dirty = rw->dirty || rc == DMFSI_OK;
        }
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_unlink(dmffs_rw_t* rw, const char* path)
{
    if (!valid(rw) || path == NULL || !dmffs_medium_writable(rw->medium))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = find(rw, path, &l);
    if (rc == DMFSI_OK)
    {
        rc = to_dmfsi(dmffs_tree_remove(l.node));       /* (a file or an empty directory) */
        rw->dirty = rw->dirty || rc == DMFSI_OK;
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_rename(dmffs_rw_t* rw, const char* old_path, const char* new_path)
{
    if (!valid(rw) || old_path == NULL || new_path == NULL || !dmffs_medium_writable(rw->medium))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t from, to;
    dmosi_mutex_lock(rw->lock);
    int rc = find(rw, old_path, &from);
    if (rc == DMFSI_OK)
    {
        rc = to_dmfsi(dmffs_tree_lookup(rw->root, new_path, &to));
        if (rc == DMFSI_OK)
        {
            if (to.parent == NULL)
                rc = DMFSI_ERR_INVALID;
            else if (!fits(rw, strlen(to.leaf)))
                rc = DMFSI_ERR_NO_SPACE;
            else
            {
                rc = to_dmfsi(dmffs_tree_move(from.node, to.parent, to.leaf));
                rw->dirty = rw->dirty || rc == DMFSI_OK;
            }
            dmffs_lookup_release(&to);
        }
        dmffs_lookup_release(&from);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_chmod(dmffs_rw_t* rw, const char* path, int mode)
{
    if (!valid(rw) || path == NULL || !dmffs_medium_writable(rw->medium))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = find(rw, path, &l);
    if (rc == DMFSI_OK)
    {
        l.node->attr = ((uint32_t)mode & ATTR_USER_MASK) | (l.node->is_dir ? DMFSI_ATTR_DIRECTORY : 0u);
        rw->dirty = true;
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}

int dmffs_rw_utime(dmffs_rw_t* rw, const char* path, uint32_t atime, uint32_t mtime)
{
    (void)atime;
    if (!valid(rw) || path == NULL || !dmffs_medium_writable(rw->medium))
        return DMFSI_ERR_INVALID;
    dmffs_lookup_t l;
    dmosi_mutex_lock(rw->lock);
    int rc = find(rw, path, &l);
    if (rc == DMFSI_OK)
    {
        l.node->mtime = mtime;
        rw->dirty = true;
        dmffs_lookup_release(&l);
    }
    dmosi_mutex_unlock(rw->lock);
    return rc;
}
