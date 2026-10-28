#define ENABLE_DIF_REGISTRATIONS    ON
#include "dmod.h"
#include "dmffs_plugin.h"
#include <string.h>

static bool resolve(dmffs_plugin_t* p)
{
    Dmod_Context_t* m = p->module;
    p->deinit = Dmod_GetDifFunction(m, dmod_dmfsi_deinit_sig);
    p->fopen = Dmod_GetDifFunction(m, dmod_dmfsi_fopen_sig);
    p->fclose = Dmod_GetDifFunction(m, dmod_dmfsi_fclose_sig);
    p->fread = Dmod_GetDifFunction(m, dmod_dmfsi_fread_sig);
    p->fwrite = Dmod_GetDifFunction(m, dmod_dmfsi_fwrite_sig);
    p->lseek = Dmod_GetDifFunction(m, dmod_dmfsi_lseek_sig);
    p->getc = Dmod_GetDifFunction(m, dmod_dmfsi_getc_sig);
    p->putc = Dmod_GetDifFunction(m, dmod_dmfsi_putc_sig);
    p->tell = Dmod_GetDifFunction(m, dmod_dmfsi_tell_sig);
    p->eof = Dmod_GetDifFunction(m, dmod_dmfsi_eof_sig);
    p->size = Dmod_GetDifFunction(m, dmod_dmfsi_size_sig);
    p->sync = Dmod_GetDifFunction(m, dmod_dmfsi_sync_sig);
    p->fflush = Dmod_GetDifFunction(m, dmod_dmfsi_fflush_sig);
    p->opendir = Dmod_GetDifFunction(m, dmod_dmfsi_opendir_sig);
    p->readdir = Dmod_GetDifFunction(m, dmod_dmfsi_readdir_sig);
    p->closedir = Dmod_GetDifFunction(m, dmod_dmfsi_closedir_sig);
    p->mkdir = Dmod_GetDifFunction(m, dmod_dmfsi_mkdir_sig);
    p->direxists = Dmod_GetDifFunction(m, dmod_dmfsi_direxists_sig);
    p->stat = Dmod_GetDifFunction(m, dmod_dmfsi_stat_sig);
    p->unlink = Dmod_GetDifFunction(m, dmod_dmfsi_unlink_sig);
    p->rename = Dmod_GetDifFunction(m, dmod_dmfsi_rename_sig);
    p->chmod = Dmod_GetDifFunction(m, dmod_dmfsi_chmod_sig);
    p->utime = Dmod_GetDifFunction(m, dmod_dmfsi_utime_sig);
    return p->deinit && p->fopen && p->fclose && p->fread && p->fwrite && p->lseek && p->getc && p->putc && p->tell &&
           p->eof && p->size && p->sync && p->fflush && p->opendir && p->readdir && p->closedir && p->mkdir &&
           p->direxists && p->stat && p->unlink && p->rename && p->chmod && p->utime;
}

/* The module is loaded and enabled (it may be, already); its mount of the device is made */
static bool mount_device(dmffs_plugin_t* p, const char* device)
{
    if (Dmod_GetModuleContext(DMFFS_PLUGIN_MODULE) == NULL && Dmod_LoadModuleByName(DMFFS_PLUGIN_MODULE) == NULL)
        return false;
    if (!Dmod_IsModuleEnabled(DMFFS_PLUGIN_MODULE) && !Dmod_EnableModule(DMFFS_PLUGIN_MODULE, false, NULL))
        return false;
    p->module = Dmod_GetModuleContext(DMFFS_PLUGIN_MODULE);
    dmod_dmfsi_init_t init = (p->module != NULL) ? Dmod_GetDifFunction(p->module, dmod_dmfsi_init_sig) : NULL;
    if (init == NULL || !resolve(p))
        return false;
    size_t length = strlen(device);
    char* config = Dmod_Malloc(length + sizeof("device="));
    if (config == NULL)
        return false;
    memcpy(config, "device=", sizeof("device=") - 1u);
    memcpy(config + sizeof("device=") - 1u, device, length + 1u);
    p->ctx = init(config);
    Dmod_Free(config);
    return p->ctx != NULL;
}

dmffs_plugin_t* dmffs_plugin_attach(const char* device)
{
    dmffs_plugin_t* p = Dmod_Malloc(sizeof(*p));
    if (p == NULL)
        return NULL;
    memset(p, 0, sizeof(*p));
    if (!mount_device(p, device))
    {
        DMOD_LOG_WARN("dmffs: the module '%s' is not there (or cannot mount '%s'): read-only\n", DMFFS_PLUGIN_MODULE, device);
        Dmod_Free(p);
        return NULL;
    }
    return p;
}

int dmffs_plugin_detach(dmffs_plugin_t* plugin)
{
    int rc = plugin->deinit(plugin->ctx);
    Dmod_Free(plugin);
    return rc;
}
