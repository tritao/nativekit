#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Free space in an mspace: its free regions, their total and the largest, in bytes. */
typedef struct nk_host_mspace_free_space {
    size_t blocks;
    size_t total;
    size_t largest;
} nk_host_mspace_free_space;

void *nk_host_mspace_create(void *base, size_t size);
void *nk_host_mspace_malloc(void *space, size_t size);
void nk_host_mspace_free(void *space, void *pointer);
void *nk_host_mspace_realloc(void *space, void *pointer, size_t size);
void *nk_host_mspace_memalign(void *space, size_t alignment, size_t size);
size_t nk_host_mspace_usable_size(const void *pointer);
/** Walks every chunk; for diagnostics only. */
nk_host_mspace_free_space nk_host_mspace_inspect_free(void *space);

#ifdef __cplusplus
}
#endif
