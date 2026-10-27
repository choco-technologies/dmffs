#include "dmod.h"
#include "dmdrvi_ioctl.h"
#include "dmffs_medium.h"
#include <errno.h>
#include <string.h>

#define RAM_PREFIX      "ram:"
#define WRITE_PIECE     4096u

struct dmffs_medium
{
    void*    file;          /* The node; NULL for RAM */
    size_t   size;
    bool     writable;
    uint8_t* ram;           /* The store of the RAM medium */
};

/* The RAM medium: one store, shared by the mounts, kept until the module goes */
static uint8_t* g_ram;
static size_t g_ram_size;

void dmffs_medium_ram_free(void)
{
    Dmod_Free(g_ram);
    g_ram = NULL;
    g_ram_size = 0;
}

static size_t parse_size(const char* text)
{
    size_t value = 0;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text += 2;
    }
    for (; *text != '\0'; text++)
    {
        int d = (*text >= '0' && *text <= '9') ? *text - '0' : (*text >= 'a' && *text <= 'f') ? *text - 'a' + 10 :
                (*text >= 'A' && *text <= 'F') ? *text - 'A' + 10 : 99;
        if (d >= base)
            return 0;
        value = value * (size_t)base + (size_t)d;
    }
    return value;
}

static bool open_ram(dmffs_medium_t* medium, const char* text)
{
    size_t size = parse_size(text);
    if (size < 64u || size > (1u << 24))
        return false;
    if (g_ram == NULL || g_ram_size != size)
    {
        dmffs_medium_ram_free();
        g_ram = Dmod_Malloc(size);
        if (g_ram == NULL)
            return false;
        memset(g_ram, 0xFF, size);
        g_ram_size = size;
    }
    medium->ram = g_ram;
    medium->size = size;
    medium->writable = true;
    return true;
}

static bool open_node(dmffs_medium_t* medium, const char* path)
{
    medium->file = Dmod_FileOpen(path, "r+");
    if (medium->file == NULL)
    {
        medium->file = Dmod_FileOpen(path, "r");
        if (medium->file == NULL)
            return false;
    }
    else
        medium->writable = true;
    dmdrvi_block_info_t info;
    memset(&info, 0, sizeof(info));
    if (Dmod_Ioctl(medium->file, DMDRVI_IOCTL_BLOCK_GET_INFO, &info) != 0 || info.block_count == 0)
        return false;
    medium->size = (size_t)(info.block_count * info.logical_block_size);
    if ((info.flags & DMDRVI_BLOCK_FLAG_READ_ONLY) != 0 || (info.flags & DMDRVI_BLOCK_FLAG_ERASE_SUPPORTED) == 0)
        medium->writable = false;
    return true;
}

dmffs_medium_t* dmffs_medium_open(const char* device)
{
    if (device == NULL || device[0] == '\0')
        return NULL;
    dmffs_medium_t* medium = Dmod_Malloc(sizeof(*medium));
    if (medium == NULL)
        return NULL;
    memset(medium, 0, sizeof(*medium));
    bool ok = (strncmp(device, RAM_PREFIX, sizeof(RAM_PREFIX) - 1u) == 0)
              ? open_ram(medium, device + sizeof(RAM_PREFIX) - 1u) : open_node(medium, device);
    if (!ok)
    {
        dmffs_medium_close(medium);
        return NULL;
    }
    return medium;
}

void dmffs_medium_close(dmffs_medium_t* medium)
{
    if (medium == NULL)
        return;
    if (medium->file != NULL)
        Dmod_FileClose(medium->file);
    Dmod_Free(medium);
}

size_t dmffs_medium_size(const dmffs_medium_t* medium)
{
    return medium->size;
}

bool dmffs_medium_writable(const dmffs_medium_t* medium)
{
    return medium->writable;
}

int dmffs_medium_read(dmffs_medium_t* medium, uint8_t* buffer, size_t size)
{
    if (size > medium->size)
        return -EINVAL;
    if (medium->ram != NULL)
    {
        memcpy(buffer, medium->ram, size);
        return 0;
    }
    if (Dmod_FileSeek(medium->file, 0, 0) != 0)
        return -EIO;
    return Dmod_FileRead(buffer, 1, size, medium->file) == size ? 0 : -EIO;
}

static int erase_all(dmffs_medium_t* medium)
{
    if (medium->ram != NULL)
    {
        memset(medium->ram, 0xFF, medium->size);
        return 0;
    }
    dmdrvi_block_range_t range = { .offset = 0, .length = medium->size };
    return Dmod_Ioctl(medium->file, DMDRVI_IOCTL_BLOCK_ERASE, &range) == 0 ? 0 : -EIO;
}

static int program(dmffs_medium_t* medium, const uint8_t* image, size_t size)
{
    if (medium->ram != NULL)
    {
        for (size_t i = 0; i < size; i++)
            medium->ram[i] &= image[i];                  /* (a flash can only clear bits) */
        return 0;
    }
    if (Dmod_FileSeek(medium->file, 0, 0) != 0)
        return -EIO;
    for (size_t done = 0; done < size; )
    {
        size_t piece = (size - done < WRITE_PIECE) ? size - done : WRITE_PIECE;
        if (Dmod_FileWrite(image + done, 1, piece, medium->file) != piece)
            return -EIO;
        done += piece;
    }
    return 0;
}

/* What is on the medium now is the image (read back piece by piece: no second copy of it in RAM) */
static int verify(dmffs_medium_t* medium, const uint8_t* image, size_t size)
{
    uint8_t piece[256];
    if (medium->ram != NULL)
        return memcmp(medium->ram, image, size) == 0 ? 0 : -EIO;
    if (Dmod_FileSeek(medium->file, 0, 0) != 0)
        return -EIO;
    for (size_t done = 0; done < size; )
    {
        size_t n = (size - done < sizeof(piece)) ? size - done : sizeof(piece);
        if (Dmod_FileRead(piece, 1, n, medium->file) != n || memcmp(piece, image + done, n) != 0)
            return -EIO;
        done += n;
    }
    return 0;
}

int dmffs_medium_rewrite(dmffs_medium_t* medium, const uint8_t* image, size_t size)
{
    if (!medium->writable)
        return -EROFS;
    if (size > medium->size)
        return -EINVAL;
    int rc = erase_all(medium);
    if (rc == 0)
        rc = program(medium, image, size);
    return (rc == 0) ? verify(medium, image, size) : rc;
}
