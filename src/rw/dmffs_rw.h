#ifndef DMFFS_RW_H
#define DMFFS_RW_H

#include "dmfsi.h"
#include <stdint.h>

/*
 * The writable mount ("device=..."): the file system is read from the medium into RAM at the mount, every change is
 * made in RAM, and the medium is written when somebody flushes (fflush, sync) - not when a file is closed. What is
 * pending when the mount goes away is written then. The functions are the DMFSI ones of dmffs.c, returning
 * DMFSI_* codes.
 */

typedef struct dmffs_rw dmffs_rw_t;

dmffs_rw_t* dmffs_rw_open(const char* device);
int dmffs_rw_close(dmffs_rw_t* rw);                     /* writes what is pending, then frees */
int dmffs_rw_medium_address(dmffs_rw_t* rw, uintptr_t* address, size_t* size);   /* (a RAM medium) */
int dmffs_rw_commit(dmffs_rw_t* rw);                    /* writes what is pending (nothing: no write) */

int dmffs_rw_fopen(dmffs_rw_t* rw, void** fp, const char* path, int mode, int attr);
int dmffs_rw_fclose(dmffs_rw_t* rw, void* fp);
int dmffs_rw_fread(dmffs_rw_t* rw, void* fp, void* buffer, size_t size, size_t* read);
int dmffs_rw_fwrite(dmffs_rw_t* rw, void* fp, const void* buffer, size_t size, size_t* written);
dmfsi_offset_t dmffs_rw_lseek(dmffs_rw_t* rw, void* fp, dmfsi_offset_t offset, int whence);
dmfsi_offset_t dmffs_rw_tell(dmffs_rw_t* rw, void* fp);
int dmffs_rw_eof(dmffs_rw_t* rw, void* fp);
dmfsi_size_t dmffs_rw_size(dmffs_rw_t* rw, void* fp);
int dmffs_rw_getc(dmffs_rw_t* rw, void* fp);
int dmffs_rw_putc(dmffs_rw_t* rw, void* fp, int c);

int dmffs_rw_opendir(dmffs_rw_t* rw, void** dp, const char* path);
int dmffs_rw_readdir(dmffs_rw_t* rw, void* dp, dmfsi_dir_entry_t* entry);
int dmffs_rw_closedir(dmffs_rw_t* rw, void* dp);

int dmffs_rw_stat(dmffs_rw_t* rw, const char* path, dmfsi_stat_t* stat);
int dmffs_rw_direxists(dmffs_rw_t* rw, const char* path);
int dmffs_rw_mkdir(dmffs_rw_t* rw, const char* path, int mode);
int dmffs_rw_unlink(dmffs_rw_t* rw, const char* path);
int dmffs_rw_rename(dmffs_rw_t* rw, const char* old_path, const char* new_path);
int dmffs_rw_chmod(dmffs_rw_t* rw, const char* path, int mode);
int dmffs_rw_utime(dmffs_rw_t* rw, const char* path, uint32_t atime, uint32_t mtime);

#endif // DMFFS_RW_H
