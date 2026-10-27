#define DMOD_ENABLE_REGISTRATION    ON
#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod.h"
#include "dmffs.h"
#include "dmffs_rw_defs.h"
#include "dmfsi.h"
#include "dmffs_rw.h"
#include "dmffs_medium.h"
#include <string.h>

/*
 * dmffs_rw as a file system of its own (mount -t dmffs_rw -o device=<node>), which is also how dmffs uses it: the
 * DMFSI functions are the ones of dmffs_rw.c, one line each. The registration of a module is enabled in this file only.
 */

#define MAGIC_MOUNT 0x44464D52u     /* 'DFMR' */

struct dmfsi_context
{
    uint32_t     magic;
    dmffs_rw_t*  rw;
};

static dmffs_rw_t* rw_of(dmfsi_context_t ctx)
{
    return (ctx != NULL && ctx->magic == MAGIC_MOUNT) ? ctx->rw : NULL;
}

/* "device=<node>" (other keys are ignored); the value is copied to the heap */
static char* device_of(const char* config)
{
    const char* p = config;
    while (p != NULL && *p != '\0')
    {
        const char* end = p;
        while (*end != '\0' && *end != ';' && *end != ',')
            end++;
        if ((size_t)(end - p) > 7u && strncmp(p, "device=", 7) == 0)
            return dmfsi_strndup(p + 7, (size_t)(end - p) - 7u);
        p = (*end != '\0') ? end + 1 : end;
    }
    return NULL;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, dmfsi_context_t, _init, (const char* config) )
{
    char* device = device_of(config);
    dmfsi_context_t ctx = (device != NULL) ? Dmod_Malloc(sizeof(*ctx)) : NULL;
    if (ctx != NULL)
    {
        ctx->magic = MAGIC_MOUNT;
        ctx->rw = dmffs_rw_open(device);
        if (ctx->rw == NULL)
        {
            Dmod_Free(ctx);
            ctx = NULL;
        }
    }
    else
        DMOD_LOG_ERROR("dmffs_rw: 'device=<node>' is needed in the configuration\n");
    Dmod_Free(device);
    return ctx;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _context_is_valid, (dmfsi_context_t ctx) )
{
    return rw_of(ctx) != NULL;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _deinit, (dmfsi_context_t ctx) )
{
    if (rw_of(ctx) == NULL)
        return DMFSI_ERR_INVALID;
    int rc = dmffs_rw_close(ctx->rw);               // writes what is pending
    ctx->magic = 0;
    Dmod_Free(ctx);
    return rc;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _fopen, (dmfsi_context_t ctx, void** fp, const char* path, int mode, int attr) )
{
    return dmffs_rw_fopen(rw_of(ctx), fp, path, mode, attr);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _fclose, (dmfsi_context_t ctx, void* fp) )
{
    return dmffs_rw_fclose(rw_of(ctx), fp);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _fread, (dmfsi_context_t ctx, void* fp, void* buffer, size_t size, size_t* read) )
{
    return dmffs_rw_fread(rw_of(ctx), fp, buffer, size, read);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _fwrite, (dmfsi_context_t ctx, void* fp, const void* buffer, size_t size, size_t* written) )
{
    return dmffs_rw_fwrite(rw_of(ctx), fp, buffer, size, written);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmffs_rw, dmfsi_offset_t, _lseek, (dmfsi_context_t ctx, void* fp, dmfsi_offset_t offset, int whence) )
{
    return dmffs_rw_lseek(rw_of(ctx), fp, offset, whence);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _ioctl, (dmfsi_context_t ctx, void* fp, int request, void* arg) )
{
    (void)fp;
    if (request != DMFFS_RW_IOCTL_MEDIUM_ADDRESS || arg == NULL)
        return 0;
    dmffs_medium_address_t* info = arg;
    return dmffs_rw_medium_address(rw_of(ctx), &info->address, &info->size);
}

/* Flushing - a file or the whole mount - writes the device */
dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _sync, (dmfsi_context_t ctx, void* fp) )
{
    (void)fp;
    return dmffs_rw_commit(rw_of(ctx));
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _fflush, (dmfsi_context_t ctx, void* fp) )
{
    (void)fp;
    return dmffs_rw_commit(rw_of(ctx));
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _getc, (dmfsi_context_t ctx, void* fp) )
{
    return dmffs_rw_getc(rw_of(ctx), fp);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _putc, (dmfsi_context_t ctx, void* fp, int c) )
{
    return dmffs_rw_putc(rw_of(ctx), fp, c);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmffs_rw, dmfsi_offset_t, _tell, (dmfsi_context_t ctx, void* fp) )
{
    return dmffs_rw_tell(rw_of(ctx), fp);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _eof, (dmfsi_context_t ctx, void* fp) )
{
    return dmffs_rw_eof(rw_of(ctx), fp);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, dmfsi_size_t, _size, (dmfsi_context_t ctx, void* fp) )
{
    return dmffs_rw_size(rw_of(ctx), fp);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _error, (dmfsi_context_t ctx, void* fp) )
{
    (void)ctx;
    (void)fp;
    return 0;
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _opendir, (dmfsi_context_t ctx, void** dp, const char* path) )
{
    return dmffs_rw_opendir(rw_of(ctx), dp, path);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmffs_rw, int, _readdir, (dmfsi_context_t ctx, void* dp, dmfsi_dir_entry_t* entry) )
{
    return dmffs_rw_readdir(rw_of(ctx), dp, entry);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _closedir, (dmfsi_context_t ctx, void* dp) )
{
    return dmffs_rw_closedir(rw_of(ctx), dp);
}

dmod_dmfsi_dif_api_declaration( 2.0, dmffs_rw, int, _stat, (dmfsi_context_t ctx, const char* path, dmfsi_stat_t* stat) )
{
    return dmffs_rw_stat(rw_of(ctx), path, stat);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _unlink, (dmfsi_context_t ctx, const char* path) )
{
    return dmffs_rw_unlink(rw_of(ctx), path);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _rename, (dmfsi_context_t ctx, const char* oldpath, const char* newpath) )
{
    return dmffs_rw_rename(rw_of(ctx), oldpath, newpath);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _chmod, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return dmffs_rw_chmod(rw_of(ctx), path, mode);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _utime, (dmfsi_context_t ctx, const char* path, uint32_t atime, uint32_t mtime) )
{
    return dmffs_rw_utime(rw_of(ctx), path, atime, mtime);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _mkdir, (dmfsi_context_t ctx, const char* path, int mode) )
{
    return dmffs_rw_mkdir(rw_of(ctx), path, mode);
}

dmod_dmfsi_dif_api_declaration( 1.0, dmffs_rw, int, _direxists, (dmfsi_context_t ctx, const char* path) )
{
    return dmffs_rw_direxists(rw_of(ctx), path);
}

int dmod_init(const Dmod_Config_t* config)
{
    (void)config;
    return 0;
}

int dmod_deinit(void)
{
    dmffs_medium_ram_free();
    return 0;
}
