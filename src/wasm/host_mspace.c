/*
 * dlmalloc in mspace-only mode, for WasmHostAllocator: one bounded heap inside a caller-owned
 * arena. It never calls sbrk or mmap, so it cannot grow past the arena, and it defines no malloc of
 * its own. Its size-binned free lists keep the arena usable under the editor's allocate-and-free
 * churn, where a single first-fit list fragmented.
 */
#include "host_mspace.h"

#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_DL_PREFIX 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define USE_LOCKS 0
#define MALLOC_ALIGNMENT 16
#define MALLOC_INSPECT_ALL 1
#define NO_MALLOC_STATS 1
#ifndef __EMSCRIPTEN__
#define DLMALLOC_EXPORT static
#endif
#include "../../vendor/dlmalloc/dlmalloc.c"

void *nk_host_mspace_create(void *base, size_t size) {
    return create_mspace_with_base(base, size, 0);
}
void *nk_host_mspace_malloc(void *space, size_t size) {
    return mspace_malloc(space, size);
}
void nk_host_mspace_free(void *space, void *pointer) {
    mspace_free(space, pointer);
}
void *nk_host_mspace_realloc(void *space, void *pointer, size_t size) {
    return mspace_realloc(space, pointer, size);
}
void *nk_host_mspace_memalign(void *space, size_t alignment, size_t size) {
    return mspace_memalign(space, alignment, size);
}
size_t nk_host_mspace_usable_size(const void *pointer) {
    return mspace_usable_size(pointer);
}

static void largest_free(void *start, void *end, size_t used, void *argument) {
    nk_host_mspace_free_space *space = (nk_host_mspace_free_space *)argument;
    size_t size = (size_t)((char *)end - (char *)start);
    if (used != 0)
        return;
    space->blocks++;
    space->total += size;
    if (size > space->largest)
        space->largest = size;
}

nk_host_mspace_free_space nk_host_mspace_inspect_free(void *space) {
    nk_host_mspace_free_space result = {0, 0, 0};
    mspace_inspect_all(space, largest_free, &result);
    return result;
}
