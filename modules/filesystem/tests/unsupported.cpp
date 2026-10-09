#include "nativekit_filesystem.h"

#include <cstdio>

int main() {
    nk_filesystem_handle root = 123;
    if (nk_filesystem_root_open("/", &root) != NK_ERROR_UNSUPPORTED || root != 0) {
        std::fprintf(stderr, "unsupported filesystem backend did not reject root access\n");
        return 1;
    }
    return 0;
}
