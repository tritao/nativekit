#include "nativekit.h"
#include "nativekit_library.h"
#include "nativekit_window.h"

#include <cassert>
#include <string>

namespace {
std::string file_uri(const char *path) {
    std::string uri = "file://";
#if defined(_WIN32)
    uri += '/';
#endif
    for (; *path; ++path)
        uri += *path == '\\' ? '/' : *path;
    return uri;
}
} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);

    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.application_id = "dev.nativekit.library-tests";
    assert(nk_init(&options) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_DYNAMIC_LIBRARY) != 0);

    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    const auto uri = file_uri(argv[1]);
    resource.uri = uri.c_str();

    nk_library library = NK_INVALID_HANDLE;
    assert(nk_library_open(&resource, &library) == NK_OK);
    assert(library != NK_INVALID_HANDLE);

    void *symbol = nullptr;
    assert(nk_library_symbol(library, "nativekit_test_library_value", &symbol) == NK_OK);
    assert(symbol != nullptr);
    assert(*static_cast<int *>(symbol) == 42);
    assert(nk_library_symbol(library, "nativekit_missing_symbol", &symbol) == NK_ERROR_NOT_FOUND);
    assert(symbol == nullptr);

    assert(nk_library_close(library) == NK_OK);
    assert(nk_library_close(library) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
