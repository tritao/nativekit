/* A host that calls exit() after nk_init() without nk_shutdown() must exit cleanly: on backends
   with a render thread, its static destruction used to call std::terminate. */
#include "nativekit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    nk_init_options options;
    memset(&options, 0, sizeof options);
    options.struct_size = sizeof options;
    options.api_version = NK_API_VERSION;
    if (nk_init(&options) != NK_OK) {
        fprintf(stderr, "nk_init failed: %s\n", nk_last_error());
        return 1;
    }
    exit(0);
}
