#define DMOD_ENABLE_REGISTRATION ON
#define ENABLE_DIF_REGISTRATIONS ON
#include "dmod_test.h"
#include "dmfsi.h"
#include <string.h>

/*
 * A writable mount through the DMFSI functions of the module - what dmvfs calls - on a medium in RAM
 * ("device=ram:32768", the same one for every mount). Changes stay in RAM until somebody flushes.
 */
#define DEVICE      "device=ram:32768"

static dmod_dmfsi_init_t g_init;
static dmod_dmfsi_deinit_t g_deinit;
static dmod_dmfsi_fopen_t g_fopen;
static dmod_dmfsi_fclose_t g_fclose;
static dmod_dmfsi_fread_t g_fread;
static dmod_dmfsi_fwrite_t g_fwrite;
static dmod_dmfsi_lseek_t g_lseek;
static dmod_dmfsi_tell_t g_tell;
static dmod_dmfsi_eof_t g_eof;
static dmod_dmfsi_size_t g_size;
static dmod_dmfsi_getc_t g_getc;
static dmod_dmfsi_putc_t g_putc;
static dmod_dmfsi_fflush_t g_fflush;
static dmod_dmfsi_sync_t g_sync;
static dmod_dmfsi_opendir_t g_opendir;
static dmod_dmfsi_readdir_t g_readdir;
static dmod_dmfsi_closedir_t g_closedir;
static dmod_dmfsi_stat_t g_stat;
static dmod_dmfsi_mkdir_t g_mkdir;
static dmod_dmfsi_direxists_t g_direxists;
static dmod_dmfsi_unlink_t g_unlink;
static dmod_dmfsi_rename_t g_rename;
static dmod_dmfsi_chmod_t g_chmod;
static dmod_dmfsi_utime_t g_utime;

static dmfsi_context_t g_fs;

static bool load_functions(void)
{
    if (Dmod_LoadModuleByName("dmffs") == NULL || !Dmod_EnableModule("dmffs", false, NULL))
        return false;
    Dmod_Context_t* m = Dmod_GetModuleContext("dmffs");
    g_init = Dmod_GetDifFunction(m, dmod_dmfsi_init_sig);
    g_deinit = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    g_fopen = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    g_fclose = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    g_fread = Dmod_GetDifFunction(m, dmod_dmfsi_fread_sig);
    g_fwrite = Dmod_GetDifFunction(m, dmod_dmfsi_fwrite_sig);
    g_lseek = Dmod_GetDifFunction(m, dmod_dmfsi_lseek_sig);
    g_tell = Dmod_GetDifFunction(m, dmod_dmfsi_tell_sig);
    g_eof = Dmod_GetDifFunction(m, dmod_dmfsi_eof_sig);
    g_size = Dmod_GetDifFunction(m, dmod_dmfsi_size_sig);
    g_getc = Dmod_GetDifFunction(m, dmod_dmfsi_getc_sig);
    g_putc = Dmod_GetDifFunction(m, dmod_dmfsi_putc_sig);
    g_fflush = Dmod_GetDifFunction(m, dmod_dmfsi_fflush_sig);
    g_sync = Dmod_GetDifFunction(m, dmod_dmfsi_sync_sig);
    g_opendir = Dmod_GetDifFunction(m, dmod_dmfsi_opendir_sig);
    g_readdir = Dmod_GetDifFunction(m, dmod_dmfsi_readdir_sig);
    g_closedir = Dmod_GetDifFunction(m, dmod_dmfsi_closedir_sig);
    g_stat = Dmod_GetDifFunction(m, dmod_dmfsi_stat_sig);
    g_mkdir = Dmod_GetDifFunction(m, dmod_dmfsi_mkdir_sig);
    g_direxists = Dmod_GetDifFunction(m, dmod_dmfsi_direxists_sig);
    g_unlink = Dmod_GetDifFunction(m, dmod_dmfsi_unlink_sig);
    g_rename = Dmod_GetDifFunction(m, dmod_dmfsi_rename_sig);
    g_chmod = Dmod_GetDifFunction(m, dmod_dmfsi_chmod_sig);
    g_utime = Dmod_GetDifFunction(m, dmod_dmfsi_utime_sig);
    return g_init && g_deinit && g_fopen && g_fclose && g_fread && g_fwrite && g_lseek && g_tell && g_eof && g_size &&
           g_getc && g_putc && g_fflush && g_sync && g_opendir && g_readdir && g_closedir && g_stat && g_mkdir &&
           g_direxists && g_unlink && g_rename && g_chmod && g_utime;
}

void dmod_test_setup(void)
{
    DMOD_TEST_EXPECT_TRUE(load_functions());
    /* A new, erased medium for every step: the store is replaced when the size differs */
    dmfsi_context_t blank = g_init("device=ram:4096");
    DMOD_TEST_EXPECT_NOT_NULL(blank);
    g_deinit(blank);
    g_fs = g_init(DEVICE);
    DMOD_TEST_EXPECT_NOT_NULL(g_fs);
}

void dmod_test_teardown(void)
{
    if (g_fs != NULL)
        g_deinit(g_fs);
    g_fs = NULL;
}

/* ---- Helpers ---- */

static int write_file(dmfsi_context_t fs, const char* path, const char* text)
{
    void* fp = NULL;
    int rc = g_fopen(fs, &fp, path, DMFSI_O_WRONLY | DMFSI_O_CREAT | DMFSI_O_TRUNC, 0);
    if (rc != DMFSI_OK)
        return rc;
    size_t written = 0;
    rc = g_fwrite(fs, fp, text, strlen(text), &written);
    g_fclose(fs, fp);
    return (rc == DMFSI_OK && written == strlen(text)) ? DMFSI_OK : DMFSI_ERR_GENERAL;
}

/* The content of a file; the length (-1: cannot be opened) */
static int read_file(dmfsi_context_t fs, const char* path, char* buffer, size_t capacity)
{
    void* fp = NULL;
    if (g_fopen(fs, &fp, path, DMFSI_O_RDONLY, 0) != DMFSI_OK)
        return -1;
    size_t n = 0;
    int rc = g_fread(fs, fp, buffer, capacity - 1, &n);
    g_fclose(fs, fp);
    buffer[n] = '\0';
    return rc == DMFSI_OK ? (int)n : -1;
}

static int list(dmfsi_context_t fs, const char* path, char* names, size_t capacity)
{
    void* dp = NULL;
    names[0] = '\0';
    if (g_opendir(fs, &dp, path) != DMFSI_OK)
        return -1;
    int count = 0;
    dmfsi_dir_entry_t entry;
    while (g_readdir(fs, dp, &entry) == DMFSI_OK)
    {
        if (strlen(names) + strlen(entry.name) + 2 < capacity)
        {
            strcat(names, entry.name);
            strcat(names, " ");
        }
        count++;
    }
    g_closedir(fs, dp);
    return count;
}

/* ---- Steps ---- */

DMOD_TEST_STEP(a_new_device_is_an_empty_file_system)
{
    char names[64];
    DMOD_TEST_EXPECT_EQ(list(g_fs, "/", names, sizeof(names)), 0);
    DMOD_TEST_EXPECT_EQ(g_direxists(g_fs, "/"), 1);
    DMOD_TEST_EXPECT_EQ(g_direxists(g_fs, "/nodir"), 0);
}

DMOD_TEST_STEP(a_file_is_written_and_read_back)
{
    char text[64];
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/hello.txt", "Hello, flash!"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/hello.txt", text, sizeof(text)), 13);
    DMOD_TEST_EXPECT_EQ(strcmp(text, "Hello, flash!"), 0);
    dmfsi_stat_t stat;
    DMOD_TEST_EXPECT_EQ(g_stat(g_fs, "/hello.txt", &stat), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(stat.size, 13u);
    DMOD_TEST_EXPECT_EQ(g_stat(g_fs, "/none", &stat), DMFSI_ERR_NOT_FOUND);
}

DMOD_TEST_STEP(nothing_reaches_the_device_until_a_flush)
{
    char text[64];
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/a.txt", "pending"), DMFSI_OK);
    dmfsi_context_t other = g_init(DEVICE);                                      /* Another mount of the same device */
    DMOD_TEST_EXPECT_NOT_NULL(other);
    DMOD_TEST_EXPECT_EQ(read_file(other, "/a.txt", text, sizeof(text)), -1);      /* ... does not see it */
    g_deinit(other);
    DMOD_TEST_EXPECT_EQ(g_fflush(g_fs, NULL), DMFSI_OK);                          /* The flush writes it */
    other = g_init(DEVICE);
    DMOD_TEST_EXPECT_EQ(read_file(other, "/a.txt", text, sizeof(text)), 7);
    DMOD_TEST_EXPECT_EQ(strcmp(text, "pending"), 0);
    g_deinit(other);
}

DMOD_TEST_STEP(closing_a_file_does_not_write_it)
{
    char text[32];
    void* fp = NULL;
    size_t n;
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/c.txt", DMFSI_O_RDWR | DMFSI_O_CREAT, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fwrite(g_fs, fp, "abc", 3, &n), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    dmfsi_context_t other = g_init(DEVICE);
    DMOD_TEST_EXPECT_EQ(read_file(other, "/c.txt", text, sizeof(text)), -1);
    g_deinit(other);
}

DMOD_TEST_STEP(sync_flushes_too)
{
    char text[32];
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/s.txt", "sync"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_sync(g_fs, NULL), DMFSI_OK);
    dmfsi_context_t other = g_init(DEVICE);
    DMOD_TEST_EXPECT_EQ(read_file(other, "/s.txt", text, sizeof(text)), 4);
    g_deinit(other);
}

DMOD_TEST_STEP(what_is_pending_is_written_when_the_mount_goes)
{
    char text[32];
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/last.txt", "bye"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_deinit(g_fs), DMFSI_OK);
    g_fs = g_init(DEVICE);
    DMOD_TEST_EXPECT_NOT_NULL(g_fs);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/last.txt", text, sizeof(text)), 3);
}

DMOD_TEST_STEP(directories_are_made_listed_and_removed)
{
    char names[128];
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/cfg", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/cfg", 0), DMFSI_ERR_EXISTS);
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/a/b", 0), DMFSI_ERR_NOT_FOUND);           /* The parent is missing */
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/cfg/boot.ini", "bank=a"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/top.txt", "t"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(list(g_fs, "/", names, sizeof(names)), 2);
    DMOD_TEST_EXPECT_EQ(strcmp(names, "cfg top.txt "), 0);
    DMOD_TEST_EXPECT_EQ(list(g_fs, "/cfg", names, sizeof(names)), 1);
    DMOD_TEST_EXPECT_EQ(strcmp(names, "boot.ini "), 0);
    DMOD_TEST_EXPECT_EQ(g_direxists(g_fs, "/cfg"), 1);
    DMOD_TEST_EXPECT_EQ(g_direxists(g_fs, "/top.txt"), 0);
    DMOD_TEST_EXPECT_EQ(g_unlink(g_fs, "/cfg"), DMFSI_ERR_NOT_EMPTY);
    DMOD_TEST_EXPECT_EQ(g_unlink(g_fs, "/cfg/boot.ini"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_unlink(g_fs, "/cfg"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(list(g_fs, "/cfg", names, sizeof(names)), -1);
}

DMOD_TEST_STEP(a_flushed_tree_comes_back_after_a_remount)
{
    char text[64], names[128];
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/cfg", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/cfg/bank_a", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/cfg/bank_a/state", "ok=1;tries=0"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/empty", ""), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_utime(g_fs, "/empty", 0, 4242), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_chmod(g_fs, "/cfg/bank_a/state", DMFSI_ATTR_READONLY), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_sync(g_fs, NULL), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_deinit(g_fs), DMFSI_OK);
    g_fs = g_init(DEVICE);
    DMOD_TEST_EXPECT_NOT_NULL(g_fs);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/cfg/bank_a/state", text, sizeof(text)), 12);
    DMOD_TEST_EXPECT_EQ(strcmp(text, "ok=1;tries=0"), 0);
    dmfsi_stat_t stat;
    DMOD_TEST_EXPECT_EQ(g_stat(g_fs, "/cfg/bank_a/state", &stat), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE((stat.attr & DMFSI_ATTR_READONLY) != 0);
    DMOD_TEST_EXPECT_EQ(g_stat(g_fs, "/empty", &stat), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(stat.size, 0u);
    DMOD_TEST_EXPECT_EQ(stat.mtime, 4242u);
    DMOD_TEST_EXPECT_EQ(list(g_fs, "/", names, sizeof(names)), 2);
}

DMOD_TEST_STEP(open_modes)
{
    char text[32];
    void* fp = NULL;
    size_t n;
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/none", DMFSI_O_RDONLY, 0), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/none", DMFSI_O_WRONLY, 0), DMFSI_ERR_NOT_FOUND);       /* (no CREAT) */
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/f", "0123456789"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/f", DMFSI_O_WRONLY | DMFSI_O_APPEND, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fwrite(g_fs, fp, "ab", 2, &n), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/f", text, sizeof(text)), 12);
    DMOD_TEST_EXPECT_EQ(strcmp(text, "0123456789ab"), 0);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/f", DMFSI_O_RDWR, 0), DMFSI_OK);                         /* Overwrite in the middle */
    DMOD_TEST_EXPECT_EQ(g_lseek(g_fs, fp, 3, DMFSI_SEEK_SET), 3);
    DMOD_TEST_EXPECT_EQ(g_fwrite(g_fs, fp, "XY", 2, &n), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_tell(g_fs, fp), 5);
    DMOD_TEST_EXPECT_EQ(g_lseek(g_fs, fp, -2, DMFSI_SEEK_END), 10);
    DMOD_TEST_EXPECT_EQ(g_getc(g_fs, fp), 'a');
    DMOD_TEST_EXPECT_EQ(g_getc(g_fs, fp), 'b');
    DMOD_TEST_EXPECT_EQ(g_eof(g_fs, fp), 1);
    DMOD_TEST_EXPECT_EQ(g_getc(g_fs, fp), -1);
    DMOD_TEST_EXPECT_EQ(g_size(g_fs, fp), 12u);
    DMOD_TEST_EXPECT_EQ(g_putc(g_fs, fp, '!'), '!');                                                   /* At the end: grows it */
    DMOD_TEST_EXPECT_EQ(g_size(g_fs, fp), 13u);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/f", text, sizeof(text)), 13);
    DMOD_TEST_EXPECT_EQ(strcmp(text, "012XY56789ab!"), 0);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/f", DMFSI_O_WRONLY | DMFSI_O_TRUNC, 0), DMFSI_OK);        /* Truncating */
    DMOD_TEST_EXPECT_EQ(g_size(g_fs, fp), 0u);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/f", DMFSI_O_RDONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fwrite(g_fs, fp, "x", 1, &n), DMFSI_ERR_INVALID);                           /* A read-only handle */
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/", DMFSI_O_RDONLY, 0), DMFSI_ERR_INVALID);                /* A directory */
}

DMOD_TEST_STEP(a_read_only_file_is_not_opened_for_writing)
{
    void* fp = NULL;
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/ro", "x"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_chmod(g_fs, "/ro", DMFSI_ATTR_READONLY), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/ro", DMFSI_O_WRONLY, 0), DMFSI_ERR_INVALID);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/ro", DMFSI_O_RDONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
}

DMOD_TEST_STEP(rename_moves_files_between_directories)
{
    char text[32];
    DMOD_TEST_EXPECT_EQ(g_mkdir(g_fs, "/d", 0), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/one", "1"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/two", "22"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_rename(g_fs, "/one", "/d/uno"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/d/uno", text, sizeof(text)), 1);
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/one", text, sizeof(text)), -1);
    DMOD_TEST_EXPECT_EQ(g_rename(g_fs, "/d/uno", "/two"), DMFSI_OK);                 /* A file replaces a file */
    DMOD_TEST_EXPECT_EQ(read_file(g_fs, "/two", text, sizeof(text)), 1);
    DMOD_TEST_EXPECT_EQ(g_rename(g_fs, "/nothing", "/x"), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_rename(g_fs, "/two", "/missing_dir/x"), DMFSI_ERR_NOT_FOUND);
    DMOD_TEST_EXPECT_EQ(g_rename(g_fs, "/d", "/d/inside"), DMFSI_ERR_INVALID);       /* Into itself */
}

DMOD_TEST_STEP(an_open_file_cannot_be_removed)
{
    void* fp = NULL;
    DMOD_TEST_EXPECT_EQ(write_file(g_fs, "/busy", "b"), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/busy", DMFSI_O_RDONLY, 0), DMFSI_OK);
    DMOD_TEST_EXPECT_TRUE(g_unlink(g_fs, "/busy") != DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_unlink(g_fs, "/busy"), DMFSI_OK);
}

DMOD_TEST_STEP(the_device_is_the_limit)
{
    char block[1024];
    memset(block, 'x', sizeof(block));
    void* fp = NULL;
    size_t n = 0;
    int rc = DMFSI_OK;
    DMOD_TEST_EXPECT_EQ(g_fopen(g_fs, &fp, "/big", DMFSI_O_WRONLY | DMFSI_O_CREAT, 0), DMFSI_OK);
    for (int i = 0; i < 40 && rc == DMFSI_OK; i++)              /* 40 kB do not fit in 32 kB */
        rc = g_fwrite(g_fs, fp, block, sizeof(block), &n);
    DMOD_TEST_EXPECT_EQ(rc, DMFSI_ERR_NO_SPACE);
    DMOD_TEST_EXPECT_TRUE(g_size(g_fs, fp) <= 32768u);
    DMOD_TEST_EXPECT_EQ(g_fclose(g_fs, fp), DMFSI_OK);
    DMOD_TEST_EXPECT_EQ(g_fflush(g_fs, NULL), DMFSI_OK);        /* What fits is written */
}
