#ifndef DMFFS_MEDIUM_H
#define DMFFS_MEDIUM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Where a writable file system lives: a block device node whose erase unit is the whole region (the dmflash node of
 * a 32 kB sector) - "device=/dev/dmflashx/fs" - read as a whole, rewritten as a whole (erase, program, read back).
 * "device=ram:<size>" is the same in RAM, shared by the mounts of the module: for the tests, and for a file
 * system that does not have to outlive the power. Errors are negative errno values.
 */

typedef struct dmffs_medium dmffs_medium_t;

dmffs_medium_t* dmffs_medium_open(const char* device);     /* NULL: no such device, no memory */
void dmffs_medium_close(dmffs_medium_t* medium);
size_t dmffs_medium_size(const dmffs_medium_t* medium);
bool dmffs_medium_writable(const dmffs_medium_t* medium);
int dmffs_medium_read(dmffs_medium_t* medium, uint8_t* buffer, size_t size);       /* the first `size` bytes */

/* Erases the whole medium, programs the image and reads it back: 0, -EROFS, -EIO, -EINVAL (too big) */
int dmffs_medium_rewrite(dmffs_medium_t* medium, const uint8_t* image, size_t size);

void dmffs_medium_ram_free(void);                          /* (dmod_deinit) */

#endif // DMFFS_MEDIUM_H
