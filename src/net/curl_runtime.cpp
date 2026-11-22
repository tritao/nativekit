#include "net/curl_runtime.hpp"

#include <dlfcn.h>

#include <cstring>
#include <initializer_list>

namespace nk::curl {
namespace {

template <typename Function> bool resolve(void *library, const char *name, Function &function) {
    void *address = dlsym(library, name);
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return address != nullptr;
}

bool load(Api &api) noexcept {
    void *library = nullptr;
    for (const char *name : {"libcurl.so.4", "libcurl-gnutls.so.4"}) {
        library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (library)
            break;
    }
    if (!library)
        return false;
    Code (*global_init)(long) = nullptr;
    const VersionInfo *(*version_info)(int) = nullptr;
    const bool resolved = resolve(library, "curl_global_init", global_init) &&
                          resolve(library, "curl_version_info", version_info) &&
                          resolve(library, "curl_easy_init", api.easy_init) &&
                          resolve(library, "curl_easy_setopt", api.easy_setopt) &&
                          resolve(library, "curl_easy_perform", api.easy_perform) &&
                          resolve(library, "curl_easy_getinfo", api.easy_getinfo) &&
                          resolve(library, "curl_easy_recv", api.easy_recv) &&
                          resolve(library, "curl_easy_send", api.easy_send) &&
                          resolve(library, "curl_easy_cleanup", api.easy_cleanup);
    /* The library stays loaded and initialized for the life of the process: libcurl's global
       state cannot be released while another thread may still use it. */
    if (!resolved || global_init(global_default) != ok) {
        dlclose(library);
        return false;
    }
    /* The stamp argument is ignored by libcurl; it always returns its own layout. */
    const VersionInfo *version = version_info(0);
    if (!version)
        return false;
    api.version_num = version->version_num;
    api.features = version->features;
    return true;
}

} // namespace

const Api *api() noexcept {
    static Api loaded;
    static const bool available = load(loaded);
    return available ? &loaded : nullptr;
}

} // namespace nk::curl
