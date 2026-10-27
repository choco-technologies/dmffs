#ifndef DMFFS_PLUGIN_H
#define DMFFS_PLUGIN_H

#include "dmfsi.h"

/*
 * The writable half of dmffs is a module of its own, dmffs_rw (src/rw): a complete DMFSI file system. dmffs - which
 * is in the firmware, where every byte counts - does not contain it: when a mount asks for writing ("device=..."),
 * dmffs loads dmffs_rw by name, mounts it on the device and hands all the calls over. Without the module the mount
 * stays read-only (or, with no addr=/size= to read from, is refused).
 */

typedef struct dmffs_plugin
{
    Dmod_Context_t*             module;
    dmfsi_context_t             ctx;        //!< the mount of dmffs_rw
    dmod_dmfsi_deinit_t         deinit;
    dmod_dmfsi_fopen_t          fopen;
    dmod_dmfsi_fclose_t         fclose;
    dmod_dmfsi_fread_t          fread;
    dmod_dmfsi_fwrite_t         fwrite;
    dmod_dmfsi_lseek_t          lseek;
    dmod_dmfsi_getc_t           getc;
    dmod_dmfsi_putc_t           putc;
    dmod_dmfsi_tell_t           tell;
    dmod_dmfsi_eof_t            eof;
    dmod_dmfsi_size_t           size;
    dmod_dmfsi_sync_t           sync;
    dmod_dmfsi_fflush_t         fflush;
    dmod_dmfsi_opendir_t        opendir;
    dmod_dmfsi_readdir_t        readdir;
    dmod_dmfsi_closedir_t       closedir;
    dmod_dmfsi_mkdir_t          mkdir;
    dmod_dmfsi_direxists_t      direxists;
    dmod_dmfsi_stat_t           stat;
    dmod_dmfsi_unlink_t         unlink;
    dmod_dmfsi_rename_t         rename;
    dmod_dmfsi_chmod_t          chmod;
    dmod_dmfsi_utime_t          utime;
} dmffs_plugin_t;

#define DMFFS_PLUGIN_MODULE     "dmffs_rw"

/* Loads dmffs_rw and mounts it on the device; NULL when the module is not there (or the mount fails) */
dmffs_plugin_t* dmffs_plugin_attach(const char* device);

/* Unmounts it (what is pending is written) and frees the hand-over */
int dmffs_plugin_detach(dmffs_plugin_t* plugin);

#endif // DMFFS_PLUGIN_H
