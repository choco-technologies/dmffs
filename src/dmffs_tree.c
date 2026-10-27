#include "dmffs_tree.h"
#include "dmffs_port.h"
#include "dmffs.h"
#include <errno.h>
#include <string.h>

#define ATTR_DIRECTORY  0x10u       /* DMFSI_ATTR_DIRECTORY: the same value in the image */
#define VERSION_TEXT    "1.0"

/* ---- Allocation helpers ---- */

static char* copy_string(const char* text, size_t length)
{
    char* copy = dmffs_alloc(length + 1u);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/* A larger block with the old content; the old one is freed. NULL (and the old one kept) when out of memory. */
static void* grow_block(void* old, size_t old_size, size_t new_size)
{
    void* block = dmffs_alloc(new_size);
    if (block == NULL)
        return NULL;
    if (old != NULL)
    {
        memcpy(block, old, old_size);
        dmffs_free(old);
    }
    return block;
}

/* ---- Nodes ---- */

static dmffs_node_t* node_create(const char* name, size_t name_length, bool is_dir, uint32_t attr)
{
    dmffs_node_t* node = dmffs_alloc(sizeof(*node));
    if (node == NULL)
        return NULL;
    memset(node, 0, sizeof(*node));
    node->is_dir = is_dir;
    node->attr = is_dir ? (attr | ATTR_DIRECTORY) : (attr & ~ATTR_DIRECTORY);
    if (name != NULL)
    {
        node->name = copy_string(name, name_length);
        if (node->name == NULL)
        {
            dmffs_free(node);
            return NULL;
        }
    }
    return node;
}

static void node_free(dmffs_node_t* node)
{
    for (uint32_t i = 0; i < node->child_count; i++)
        node_free(node->children[i]);
    dmffs_free(node->children);
    dmffs_free(node->data);
    dmffs_free(node->name);
    dmffs_free(node);
}

dmffs_node_t* dmffs_tree_create(void)
{
    return node_create(NULL, 0, true, 0);
}

void dmffs_tree_destroy(dmffs_node_t* root)
{
    if (root != NULL)
        node_free(root);
}

dmffs_node_t* dmffs_node_find_child(const dmffs_node_t* dir, const char* name)
{
    for (uint32_t i = 0; dir != NULL && dir->is_dir && i < dir->child_count; i++)
    {
        if (strcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    }
    return NULL;
}

static uint32_t depth_of(const dmffs_node_t* node)
{
    uint32_t depth = 0;
    for (; node->parent != NULL; node = node->parent)
        depth++;
    return depth;
}

static uint32_t height_of(const dmffs_node_t* node)
{
    uint32_t height = 0;
    for (uint32_t i = 0; node->is_dir && i < node->child_count; i++)
    {
        uint32_t h = 1u + height_of(node->children[i]);
        height = (h > height) ? h : height;
    }
    return height;
}

static bool name_valid(const char* name)
{
    size_t length = strlen(name);
    return length >= 1u && length <= DMFFS_NAME_MAX && strchr(name, '/') == NULL &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int attach(dmffs_node_t* dir, dmffs_node_t* node)
{
    if (dir->child_count == dir->child_capacity)
    {
        uint32_t capacity = dir->child_capacity ? dir->child_capacity * 2u : 4u;
        dmffs_node_t** list = grow_block(dir->children, dir->child_count * sizeof(*list), capacity * sizeof(*list));
        if (list == NULL)
            return -ENOMEM;
        dir->children = list;
        dir->child_capacity = capacity;
    }
    dir->children[dir->child_count++] = node;
    node->parent = dir;
    return 0;
}

static void detach(dmffs_node_t* node)
{
    dmffs_node_t* dir = node->parent;
    for (uint32_t i = 0; dir != NULL && i < dir->child_count; i++)
    {
        if (dir->children[i] == node)
        {
            memmove(&dir->children[i], &dir->children[i + 1u], (dir->child_count - i - 1u) * sizeof(dir->children[0]));
            dir->child_count--;
            break;
        }
    }
    node->parent = NULL;
}

int dmffs_tree_add(dmffs_node_t* dir, const char* name, bool is_dir, uint32_t attr, dmffs_node_t** result)
{
    if (dir == NULL || !dir->is_dir || name == NULL || !name_valid(name) || depth_of(dir) + (is_dir ? 1u : 0u) >= DMFFS_DEPTH_MAX + 1u)
        return -EINVAL;
    if (dmffs_node_find_child(dir, name) != NULL)
        return -EEXIST;
    dmffs_node_t* node = node_create(name, strlen(name), is_dir, attr);
    if (node == NULL)
        return -ENOMEM;
    int rc = attach(dir, node);
    if (rc != 0)
    {
        node_free(node);
        return rc;
    }
    if (result != NULL)
        *result = node;
    return 0;
}

int dmffs_tree_remove(dmffs_node_t* node)
{
    if (node == NULL || node->parent == NULL)
        return -EINVAL;
    if (node->opens != 0)
        return -EBUSY;
    if (node->is_dir && node->child_count != 0)
        return -ENOTEMPTY;
    detach(node);
    node_free(node);
    return 0;
}

static bool is_inside(const dmffs_node_t* ancestor, const dmffs_node_t* node)
{
    for (; node != NULL; node = node->parent)
    {
        if (node == ancestor)
            return true;
    }
    return false;
}

/* The place a node is moved to is free, or holds a file that a file replaces */
static int free_the_place(dmffs_node_t* node, dmffs_node_t* dir, const char* name)
{
    dmffs_node_t* old = dmffs_node_find_child(dir, name);
    if (old == NULL || old == node)
        return 0;
    if (node->is_dir || old->is_dir)
        return -EEXIST;
    return dmffs_tree_remove(old);
}

int dmffs_tree_move(dmffs_node_t* node, dmffs_node_t* dir, const char* name)
{
    if (node == NULL || node->parent == NULL || dir == NULL || !dir->is_dir || name == NULL || !name_valid(name))
        return -EINVAL;
    if (is_inside(node, dir) || depth_of(dir) + 1u + height_of(node) > DMFFS_DEPTH_MAX)
        return -EINVAL;
    if (node->opens != 0)
        return -EBUSY;
    if (dmffs_node_find_child(dir, name) == node)
        return 0;
    char* new_name = copy_string(name, strlen(name));
    if (new_name == NULL)
        return -ENOMEM;
    int rc = free_the_place(node, dir, name);
    if (rc == 0)
    {
        dmffs_node_t* old_parent = node->parent;
        detach(node);
        rc = attach(dir, node);
        if (rc != 0)
            attach(old_parent, node);           /* (it had a place, there is room for it) */
    }
    if (rc != 0)
    {
        dmffs_free(new_name);
        return rc;
    }
    dmffs_free(node->name);
    node->name = new_name;
    return 0;
}

/* ---- Paths ---- */

/* The next component of `*cursor` (NUL-terminated in place); NULL at the end */
static char* next_component(char** cursor)
{
    char* p = *cursor;
    while (*p == '/')
        p++;
    if (*p == '\0')
        return NULL;
    char* start = p;
    while (*p != '\0' && *p != '/')
        p++;
    if (*p == '/')
        *p++ = '\0';
    *cursor = p;
    return start;
}

static bool more_components(const char* cursor)
{
    while (*cursor == '/')
        cursor++;
    return *cursor != '\0';
}

int dmffs_tree_lookup(dmffs_node_t* root, const char* path, dmffs_lookup_t* lookup)
{
    if (root == NULL || path == NULL || lookup == NULL)
        return -EINVAL;
    memset(lookup, 0, sizeof(*lookup));
    lookup->buffer = copy_string(path, strlen(path));
    if (lookup->buffer == NULL)
        return -ENOMEM;
    lookup->node = root;
    char* cursor = lookup->buffer;
    char* part;
    int rc = 0;
    while (rc == 0 && (part = next_component(&cursor)) != NULL)
    {
        if (!name_valid(part))
            rc = -EINVAL;
        else if (!lookup->node->is_dir)
            rc = -ENOTDIR;
        else
        {
            lookup->parent = lookup->node;
            lookup->leaf = part;
            lookup->node = dmffs_node_find_child(lookup->parent, part);
            if (lookup->node == NULL && more_components(cursor))
                rc = -ENOENT;
        }
    }
    if (rc != 0)
        dmffs_lookup_release(lookup);
    return rc;
}

void dmffs_lookup_release(dmffs_lookup_t* lookup)
{
    if (lookup != NULL)
    {
        dmffs_free(lookup->buffer);
        memset(lookup, 0, sizeof(*lookup));
    }
}

/* ---- File content ---- */

size_t dmffs_file_read(const dmffs_node_t* file, size_t offset, void* buffer, size_t size)
{
    if (file == NULL || file->is_dir || offset >= file->size)
        return 0;
    size_t count = file->size - offset;
    count = (count < size) ? count : size;
    memcpy(buffer, file->data + offset, count);
    return count;
}

static int reserve(dmffs_node_t* file, size_t size)
{
    if (size <= file->capacity)
        return 0;
    size_t capacity = file->capacity ? file->capacity : 64u;
    while (capacity < size)
        capacity *= 2u;
    uint8_t* data = grow_block(file->data, file->size, capacity);
    if (data == NULL)
        return -ENOMEM;
    file->data = data;
    file->capacity = capacity;
    return 0;
}

int dmffs_file_truncate(dmffs_node_t* file, size_t size)
{
    if (file == NULL || file->is_dir)
        return -EINVAL;
    if (size > file->size)
    {
        int rc = reserve(file, size);
        if (rc != 0)
            return rc;
        memset(file->data + file->size, 0, size - file->size);
    }
    file->size = size;
    return 0;
}

int dmffs_file_write(dmffs_node_t* file, size_t offset, const void* buffer, size_t size)
{
    if (file == NULL || file->is_dir || offset + size < offset)
        return -EINVAL;
    if (offset > file->size)
    {
        int rc = dmffs_file_truncate(file, offset);     /* (the gap reads as zeros) */
        if (rc != 0)
            return rc;
    }
    int rc = reserve(file, offset + size);
    if (rc != 0)
        return rc;
    memcpy(file->data + offset, buffer, size);
    if (offset + size > file->size)
        file->size = offset + size;
    return 0;
}

/* ---- The image ---- */

typedef struct
{
    uint8_t* out;
    size_t   position;
    size_t   capacity;
} writer_t;

static void put(writer_t* w, const void* data, size_t size)
{
    if (size != 0 && size <= w->capacity - w->position)
        memcpy(w->out + w->position, data, size);
    w->position += size;                    /* (counted past the end too: the caller checks) */
}

static void put_u32(writer_t* w, uint32_t value)
{
    put(w, &value, sizeof(value));
}

static void put_tlv(writer_t* w, uint32_t type, const void* data, uint32_t length)
{
    put_u32(w, type);
    put_u32(w, length);
    put(w, data, length);
}

static void put_u32_tlv(writer_t* w, uint32_t type, uint32_t value)
{
    put_tlv(w, type, &value, sizeof(value));
}

static size_t content_size(const dmffs_node_t* node);

/* A FILE or a DIR entry with its header */
static size_t entry_size(const dmffs_node_t* node)
{
    return 8u + content_size(node);
}

static size_t content_size(const dmffs_node_t* node)
{
    size_t size = (8u + strlen(node->name)) + (8u + 4u) + (8u + 4u);       /* NAME, ATTR, DATE */
    if (!node->is_dir)
        return size + 8u + node->size;                                     /* DATA */
    for (uint32_t i = 0; i < node->child_count; i++)
        size += entry_size(node->children[i]);
    return size;
}

size_t dmffs_tree_image_size(const dmffs_node_t* root)
{
    size_t size = (8u + sizeof(VERSION_TEXT) - 1u) + (8u + 4u);            /* VERSION, CRC */
    for (uint32_t i = 0; i < root->child_count; i++)
        size += entry_size(root->children[i]);
    return size;
}

static void put_entry(writer_t* w, const dmffs_node_t* node)
{
    put_u32(w, node->is_dir ? DMFFS_TLV_TYPE_DIR : DMFFS_TLV_TYPE_FILE);
    put_u32(w, (uint32_t)content_size(node));
    put_tlv(w, DMFFS_TLV_TYPE_NAME, node->name, (uint32_t)strlen(node->name));
    put_u32_tlv(w, DMFFS_TLV_TYPE_ATTR, node->attr);
    put_u32_tlv(w, DMFFS_TLV_TYPE_DATE, node->mtime);
    if (!node->is_dir)
        put_tlv(w, DMFFS_TLV_TYPE_DATA, node->data, (uint32_t)node->size);
    for (uint32_t i = 0; node->is_dir && i < node->child_count; i++)
        put_entry(w, node->children[i]);
}

uint32_t dmffs_crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (size-- > 0)
    {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int dmffs_tree_serialize(const dmffs_node_t* root, uint8_t* out, size_t capacity, size_t* written)
{
    if (root == NULL || out == NULL || dmffs_tree_image_size(root) > capacity)
        return -ENOSPC;
    writer_t w = { out, 0, capacity };
    put_tlv(&w, DMFFS_TLV_TYPE_VERSION, VERSION_TEXT, sizeof(VERSION_TEXT) - 1u);
    for (uint32_t i = 0; i < root->child_count; i++)
        put_entry(&w, root->children[i]);
    put_u32_tlv(&w, DMFFS_TLV_TYPE_CRC, dmffs_crc32(out, w.position));      /* (the rest of the image stays erased: END) */
    if (written != NULL)
        *written = w.position;
    return 0;
}

/* ---- Loading ---- */

typedef struct
{
    const uint8_t* p;
    size_t         length;
} span_t;

/* The next TLV of `body` at *offset: 1 - got it, 0 - the end (erased flash, no type, or the end of the span), -EBADMSG */
static int next_tlv(const span_t* body, size_t* offset, uint32_t* type, span_t* value)
{
    if (*offset + 8u > body->length)
        return 0;
    uint32_t length;
    memcpy(type, body->p + *offset, 4);
    memcpy(&length, body->p + *offset + 4u, 4);
    if (*type == DMFFS_TLV_TYPE_END || *type == DMFFS_TLV_TYPE_INVALID)
        return 0;
    if (length > body->length - *offset - 8u)
        return -EBADMSG;
    value->p = body->p + *offset + 8u;
    value->length = length;
    *offset += 8u + length;
    return 1;
}

typedef struct
{
    span_t   name;
    span_t   data;
    uint32_t attr;
    uint32_t mtime;
} fields_t;

static int read_fields(const span_t* body, fields_t* f)
{
    memset(f, 0, sizeof(*f));
    size_t offset = 0;
    uint32_t type;
    span_t value;
    int rc;
    while ((rc = next_tlv(body, &offset, &type, &value)) > 0)
    {
        if (type == DMFFS_TLV_TYPE_NAME)
            f->name = value;
        else if (type == DMFFS_TLV_TYPE_DATA)
            f->data = value;
        else if (type == DMFFS_TLV_TYPE_ATTR && value.length >= 4u)
            memcpy(&f->attr, value.p, 4);
        else if (type == DMFFS_TLV_TYPE_DATE && value.length >= 4u)
            memcpy(&f->mtime, value.p, 4);
    }
    return rc;
}

static int load_children(dmffs_node_t* dir, const span_t* body);

/* An entry of the image becomes a node of `dir` */
static int load_entry(dmffs_node_t* dir, bool is_dir, const span_t* body)
{
    fields_t f;
    if (read_fields(body, &f) != 0 || f.name.p == NULL)
        return -EBADMSG;
    char* name = copy_string((const char*)f.name.p, f.name.length);
    if (name == NULL)
        return -ENOMEM;
    dmffs_node_t* node = NULL;
    int rc = dmffs_tree_add(dir, name, is_dir, f.attr, &node);
    dmffs_free(name);
    if (rc != 0)
        return (rc == -ENOMEM) ? rc : -EBADMSG;           /* (a bad or repeated name, too deep: not an image of ours) */
    node->mtime = f.mtime;
    if (is_dir)
        return load_children(node, body);
    if (f.data.length != 0)
        return dmffs_file_write(node, 0, f.data.p, f.data.length);
    return 0;
}

/* The FILE and DIR entries of `body` (anything else - NAME, VERSION, ... - is skipped) */
static int load_children(dmffs_node_t* dir, const span_t* body)
{
    size_t offset = 0;
    uint32_t type;
    span_t value;
    int rc;
    while ((rc = next_tlv(body, &offset, &type, &value)) > 0)
    {
        if (type == DMFFS_TLV_TYPE_FILE || type == DMFFS_TLV_TYPE_DIR)
        {
            rc = load_entry(dir, type == DMFFS_TLV_TYPE_DIR, &value);
            if (rc != 0)
                return rc;
        }
    }
    return rc;
}

/* The CRC entry, if the image has one, has to match everything before it; what follows it is erased flash */
static int check_crc(const uint8_t* image, size_t size)
{
    span_t body = { image, size };
    size_t offset = 0, start;
    uint32_t type;
    span_t value;
    int rc;
    do
    {
        start = offset;
        rc = next_tlv(&body, &offset, &type, &value);
    } while (rc > 0 && type != DMFFS_TLV_TYPE_CRC);
    if (rc < 0)
        return -EBADMSG;
    if (rc == 0)
        return 0;                                        /* (an image of make_dmffs: no CRC) */
    uint32_t expected;
    if (value.length != 4u)
        return -EBADMSG;
    memcpy(&expected, value.p, 4);
    return dmffs_crc32(image, start) == expected ? 0 : -EBADMSG;
}

int dmffs_tree_load(dmffs_node_t* root, const uint8_t* image, size_t size)
{
    if (root == NULL || !root->is_dir || root->child_count != 0 || image == NULL)
        return -EINVAL;
    uint32_t first = 0;
    if (size >= 4u)
        memcpy(&first, image, 4);
    if (size < 8u || first == 0xFFFFFFFFu)
        return 1;                                        /* Erased: nothing was ever written */
    int rc = check_crc(image, size);
    if (rc != 0)
        return rc;
    span_t body = { image, size };
    rc = load_children(root, &body);
    if (rc != 0)
    {
        for (uint32_t i = 0; i < root->child_count; i++)
            node_free(root->children[i]);
        dmffs_free(root->children);
        root->children = NULL;
        root->child_count = root->child_capacity = 0;
    }
    return rc;
}
