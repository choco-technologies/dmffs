#ifndef DMFFS_PORT_H
#define DMFFS_PORT_H

/*
 * What the tree of a writable mount (dmffs_tree.c) needs: an allocator. In the module it is DMOD's; the unit tests
 * of the tree on the host (tests/tree_test.c) bring their own, a boot loader its own.
 */
#include "dmod.h"

#define dmffs_alloc(size)   Dmod_Malloc(size)
#define dmffs_free(ptr)     Dmod_Free(ptr)

#endif // DMFFS_PORT_H
