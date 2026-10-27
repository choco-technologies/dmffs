/*
 * The tree of a writable mount on the host: no DMOD, no module loader - the same source file the module is built
 * from (src/dmffs_tree.c) with an allocator of libc. Exit code = the number of failed checks.
 */
#include "dmffs_tree.h"
#include "dmffs.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int g_failed;

#define CHECK(condition)  do { if (!(condition)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); g_failed++; } } while (0)

static dmffs_node_t* add(dmffs_node_t* dir, const char* name, bool is_dir)
{
    dmffs_node_t* node = NULL;
    int rc = dmffs_tree_add(dir, name, is_dir, 0, &node);
    CHECK(rc == 0);
    return node;
}

static dmffs_node_t* file_with(dmffs_node_t* dir, const char* name, const void* data, size_t size)
{
    dmffs_node_t* file = add(dir, name, false);
    CHECK(dmffs_file_write(file, 0, data, size) == 0);
    return file;
}

static int lookup(dmffs_node_t* root, const char* path, dmffs_node_t** node)
{
    dmffs_lookup_t l;
    int rc = dmffs_tree_lookup(root, path, &l);
    if (rc == 0)
    {
        *node = l.node;
        dmffs_lookup_release(&l);
    }
    return rc;
}

static void test_names_and_paths(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    dmffs_node_t* dir = add(root, "etc", true);
    dmffs_node_t* f = file_with(dir, "boot.ini", "x", 1);
    dmffs_node_t* found = NULL;
    CHECK(lookup(root, "/etc/boot.ini", &found) == 0 && found == f);
    CHECK(lookup(root, "etc//boot.ini", &found) == 0 && found == f);
    CHECK(lookup(root, "", &found) == 0 && found == root);
    CHECK(lookup(root, "/", &found) == 0 && found == root);
    CHECK(lookup(root, "etc/none", &found) == 0 && found == NULL);          /* (the last component only is missing) */
    CHECK(lookup(root, "nodir/none", &found) == -ENOENT);
    CHECK(lookup(root, "etc/boot.ini/x", &found) == -ENOTDIR);
    CHECK(lookup(root, "etc/../boot.ini", &found) == -EINVAL);
    CHECK(dmffs_tree_add(root, "a/b", false, 0, NULL) == -EINVAL);
    CHECK(dmffs_tree_add(root, "", false, 0, NULL) == -EINVAL);
    CHECK(dmffs_tree_add(root, "etc", true, 0, NULL) == -EEXIST);
    char long_name[300];
    memset(long_name, 'n', sizeof(long_name));
    long_name[256] = '\0';
    CHECK(dmffs_tree_add(root, long_name, false, 0, NULL) == -EINVAL);
    long_name[255] = '\0';
    CHECK(dmffs_tree_add(root, long_name, false, 0, NULL) == 0);
    dmffs_tree_destroy(root);
}

static void test_file_content(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    dmffs_node_t* f = add(root, "f", false);
    char buffer[16];
    CHECK(dmffs_file_write(f, 0, "hello", 5) == 0 && f->size == 5);
    CHECK(dmffs_file_write(f, 2, "LLO!", 4) == 0 && f->size == 6);
    CHECK(dmffs_file_read(f, 0, buffer, sizeof(buffer)) == 6 && memcmp(buffer, "heLLO!", 6) == 0);
    CHECK(dmffs_file_write(f, 10, "z", 1) == 0 && f->size == 11);                       /* A gap reads as zeros */
    CHECK(dmffs_file_read(f, 6, buffer, 4) == 4 && buffer[0] == 0 && buffer[3] == 0);
    CHECK(dmffs_file_read(f, 11, buffer, 4) == 0);
    CHECK(dmffs_file_truncate(f, 3) == 0 && f->size == 3);
    CHECK(dmffs_file_truncate(f, 0) == 0 && f->size == 0);
    uint8_t big[5000];
    memset(big, 0x5A, sizeof(big));
    CHECK(dmffs_file_write(f, 0, big, sizeof(big)) == 0 && f->size == sizeof(big));      /* (it grew past its first block) */
    CHECK(dmffs_file_write(root, 0, "x", 1) == -EINVAL);                                 /* A directory has no content */
    dmffs_tree_destroy(root);
}

static void test_remove_and_move(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    dmffs_node_t* a = add(root, "a", true);
    dmffs_node_t* b = add(root, "b", true);
    dmffs_node_t* f = file_with(a, "f", "1", 1);
    CHECK(dmffs_tree_remove(a) == -ENOTEMPTY);
    CHECK(dmffs_tree_remove(root) == -EINVAL);
    f->opens = 1;
    CHECK(dmffs_tree_remove(f) == -EBUSY);
    CHECK(dmffs_tree_move(f, b, "g") == -EBUSY);
    f->opens = 0;
    CHECK(dmffs_tree_move(f, b, "g") == 0 && strcmp(f->name, "g") == 0 && f->parent == b && a->child_count == 0);
    dmffs_node_t* other = file_with(b, "h", "22", 2);
    CHECK(dmffs_tree_move(other, b, "g") == 0);                                          /* A file replaces a file */
    CHECK(b->child_count == 1 && dmffs_node_find_child(b, "g")->size == 2);
    CHECK(dmffs_tree_move(a, b, "g") == -EEXIST);                                        /* ... but not over a file with a directory */
    CHECK(dmffs_tree_move(a, a, "x") == -EINVAL);                                        /* Into itself */
    CHECK(dmffs_tree_move(b, a, "b") == 0);
    CHECK(dmffs_tree_move(a, dmffs_node_find_child(a, "b"), "a") == -EINVAL);            /* Into its own subtree */
    CHECK(dmffs_tree_remove(dmffs_node_find_child(dmffs_node_find_child(a, "b"), "g")) == 0);
    CHECK(dmffs_tree_remove(dmffs_node_find_child(a, "b")) == 0);
    CHECK(dmffs_tree_remove(a) == 0 && root->child_count == 0);
    dmffs_tree_destroy(root);
}

static void test_depth(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    dmffs_node_t* dir = root;
    for (unsigned i = 0; i < DMFFS_DEPTH_MAX; i++)
        dir = add(dir, "d", true);
    CHECK(dmffs_tree_add(dir, "d", true, 0, NULL) == -EINVAL);                           /* One too deep */
    CHECK(dmffs_tree_add(dir, "file", false, 0, NULL) == 0);                             /* A file there is fine */
    dmffs_tree_destroy(root);
}

static bool same(const dmffs_node_t* a, const dmffs_node_t* b)
{
    if (a->is_dir != b->is_dir || a->attr != b->attr || a->mtime != b->mtime || a->size != b->size ||
        a->child_count != b->child_count || (a->size && memcmp(a->data, b->data, a->size) != 0))
        return false;
    for (uint32_t i = 0; i < a->child_count; i++)
    {
        if (strcmp(a->children[i]->name, b->children[i]->name) != 0 || !same(a->children[i], b->children[i]))
            return false;
    }
    return true;
}

static dmffs_node_t* sample_tree(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    uint8_t binary[300];
    for (unsigned i = 0; i < sizeof(binary); i++)
        binary[i] = (uint8_t)(i * 7u);
    file_with(root, "readme.txt", "hello\n", 6)->mtime = 1234;
    add(root, "empty.bin", false);
    dmffs_node_t* cfg = add(root, "cfg", true);
    cfg->mtime = 99;
    file_with(cfg, "boot.ini", binary, sizeof(binary))->attr = 0x01;
    add(cfg, "empty_dir", true);
    dmffs_node_t* deep = add(add(cfg, "a", true), "b", true);
    file_with(deep, "x", "deep", 4);
    return root;
}

static void test_roundtrip(void)
{
    dmffs_node_t* root = sample_tree();
    size_t size = dmffs_tree_image_size(root), written = 0;
    uint8_t* image = malloc(size + 64);
    memset(image, 0xFF, size + 64);                                                      /* (erased flash after the image) */
    CHECK(dmffs_tree_serialize(root, image, size - 1, &written) == -ENOSPC);
    CHECK(dmffs_tree_serialize(root, image, size, &written) == 0 && written == size);
    dmffs_node_t* copy = dmffs_tree_create();
    CHECK(dmffs_tree_load(copy, image, size + 64) == 0);
    CHECK(same(root, copy));
    dmffs_tree_destroy(copy);

    /* A flipped bit anywhere in the image: the CRC or the structure says so */
    for (size_t at = 0; at < size; at += 7)
    {
        image[at] ^= 0x04;
        copy = dmffs_tree_create();
        CHECK(dmffs_tree_load(copy, image, size + 64) == -EBADMSG);
        CHECK(copy->child_count == 0);                                                   /* (nothing half loaded is left) */
        dmffs_tree_destroy(copy);
        image[at] ^= 0x04;
    }
    /* Cut short: the CRC entry is missing or incomplete, an image of make_dmffs has none - so cut inside an entry */
    copy = dmffs_tree_create();
    CHECK(dmffs_tree_load(copy, image, size / 2) == -EBADMSG);
    dmffs_tree_destroy(copy);
    free(image);
    dmffs_tree_destroy(root);
}

static void put32(uint8_t* image, size_t* at, uint32_t value)
{
    memcpy(image + *at, &value, 4);
    *at += 4;
}

static void put_tlv(uint8_t* image, size_t* at, uint32_t type, const void* data, uint32_t length)
{
    put32(image, at, type);
    put32(image, at, length);
    memcpy(image + *at, data, length);
    *at += length;
}

/* What make_dmffs writes: VERSION, FILE { NAME, DATA }, DIR { NAME, FILE { NAME, DATA } }, END - no ATTR, no DATE, no CRC */
static void test_image_of_make_dmffs(void)
{
    uint8_t image[256];
    size_t at = 0;
    memset(image, 0xFF, sizeof(image));
    put_tlv(image, &at, DMFFS_TLV_TYPE_VERSION, "1.0", 3);
    put32(image, &at, DMFFS_TLV_TYPE_FILE);
    put32(image, &at, 8 + 5 + 8 + 2);
    put_tlv(image, &at, DMFFS_TLV_TYPE_NAME, "hello", 5);
    put_tlv(image, &at, DMFFS_TLV_TYPE_DATA, "Hi", 2);
    put32(image, &at, DMFFS_TLV_TYPE_DIR);
    put32(image, &at, (8 + 3) + (8 + (8 + 1) + (8 + 1)));
    put_tlv(image, &at, DMFFS_TLV_TYPE_NAME, "dir", 3);
    put32(image, &at, DMFFS_TLV_TYPE_FILE);
    put32(image, &at, (8 + 1) + (8 + 1));
    put_tlv(image, &at, DMFFS_TLV_TYPE_NAME, "f", 1);
    put_tlv(image, &at, DMFFS_TLV_TYPE_DATA, "!", 1);
    put32(image, &at, DMFFS_TLV_TYPE_END);
    put32(image, &at, 0);
    dmffs_node_t* root = dmffs_tree_create();
    CHECK(dmffs_tree_load(root, image, sizeof(image)) == 0);
    dmffs_node_t* found = NULL;
    CHECK(lookup(root, "hello", &found) == 0 && found != NULL && found->size == 2 && memcmp(found->data, "Hi", 2) == 0);
    CHECK(lookup(root, "dir/f", &found) == 0 && found != NULL && found->size == 1 && found->data[0] == '!');
    dmffs_tree_destroy(root);
}

static void test_blank_and_garbage(void)
{
    uint8_t image[64];
    memset(image, 0xFF, sizeof(image));
    dmffs_node_t* root = dmffs_tree_create();
    CHECK(dmffs_tree_load(root, image, sizeof(image)) == 1);                             /* Erased flash: an empty file system */
    CHECK(dmffs_tree_load(root, image, 0) == 1);
    memset(image, 0x00, sizeof(image));
    CHECK(dmffs_tree_load(root, image, sizeof(image)) == 0 && root->child_count == 0);   /* (no type: the end) */
    for (unsigned i = 0; i < sizeof(image); i++)
        image[i] = (uint8_t)(i * 37u + 11u);
    CHECK(dmffs_tree_load(root, image, sizeof(image)) == -EBADMSG);
    dmffs_tree_destroy(root);
}

static void test_too_deep_image(void)
{
    dmffs_node_t* root = dmffs_tree_create();
    dmffs_node_t* dir = root;
    for (unsigned i = 0; i < DMFFS_DEPTH_MAX; i++)
        dir = add(dir, "d", true);
    size_t size = dmffs_tree_image_size(root);
    uint8_t* image = malloc(size);
    CHECK(dmffs_tree_serialize(root, image, size, NULL) == 0);
    dmffs_node_t* copy = dmffs_tree_create();
    CHECK(dmffs_tree_load(copy, image, size) == 0);                                      /* The limit itself is fine */
    dmffs_tree_destroy(copy);
    free(image);
    dmffs_tree_destroy(root);
}

int main(void)
{
    test_names_and_paths();
    test_file_content();
    test_remove_and_move();
    test_depth();
    test_roundtrip();
    test_image_of_make_dmffs();
    test_blank_and_garbage();
    test_too_deep_image();
    printf(g_failed == 0 ? "tree_test: all passed\n" : "tree_test: %d check(s) failed\n", g_failed);
    return g_failed;
}
