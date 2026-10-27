#ifndef STUB_DMOD_H
#define STUB_DMOD_H

/* The little of DMOD that the tree (src/dmffs_tree.c) uses, for its test on the host */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define Dmod_Malloc(size)   malloc(size)
#define Dmod_Free(ptr)      free(ptr)
#define DMOD_LOG_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define DMOD_LOG_WARN(...)  fprintf(stderr, __VA_ARGS__)

#endif // STUB_DMOD_H
