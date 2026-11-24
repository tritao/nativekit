#include "backend.hpp"
#include "libsecret_runtime.hpp"

#include <dlfcn.h>

#include <cstring>

namespace {

namespace secret = nkc::secret;

template <typename Function> bool resolve(void *library, const char *name, Function &function) {
    void *address = dlsym(library, name);
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return address != nullptr;
}

#define NKC_RESOLVE(name) resolve(library, #name, api.name)

bool load(secret::Api &api) noexcept {
    void *library = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return false;
    /* libsecret links GLib and GIO, so the handle resolves their symbols too. */
    const bool resolved = NKC_RESOLVE(secret_password_storev_binary_sync) &&
                          NKC_RESOLVE(secret_password_lookupv_binary_sync) &&
                          NKC_RESOLVE(secret_password_clearv_sync) &&
                          NKC_RESOLVE(secret_value_new) && NKC_RESOLVE(secret_value_get) &&
                          NKC_RESOLVE(secret_value_unref) && NKC_RESOLVE(secret_error_get_quark) &&
                          NKC_RESOLVE(g_io_error_quark) && NKC_RESOLVE(g_hash_table_new_full) &&
                          NKC_RESOLVE(g_hash_table_insert) && NKC_RESOLVE(g_hash_table_unref) &&
                          NKC_RESOLVE(g_str_hash) && NKC_RESOLVE(g_str_equal) &&
                          NKC_RESOLVE(g_strdup) && NKC_RESOLVE(g_free) && NKC_RESOLVE(g_error_free);
    /* GLib types registered by libsecret cannot be unloaded, so the library stays loaded. */
    return resolved;
}

#undef NKC_RESOLVE

/** libsecret, loaded on first use; null when it is not installed. */
const secret::Api *api() noexcept {
    static secret::Api loaded{};
    static const bool available = load(loaded);
    return available ? &loaded : nullptr;
}

int32_t unavailable() {
    nkc::set_error("the operating-system credential store is unavailable: libsecret-1.so.0 is not "
                   "installed");
    return NK_ERROR_UNSUPPORTED;
}

const secret::Schema *schema() {
    static const secret::Schema value = {"org.nativekit.Credential",
                                         secret::schema_none,
                                         {{"service", secret::schema_attribute_string},
                                          {"account", secret::schema_attribute_string},
                                          {nullptr, 0}}};
    return &value;
}

secret::HashTable *attributes(const secret::Api &api, const nkc::Request &request) {
    auto *table =
        api.g_hash_table_new_full(api.g_str_hash, api.g_str_equal, api.g_free, api.g_free);
    api.g_hash_table_insert(table, api.g_strdup("service"), api.g_strdup(request.service));
    api.g_hash_table_insert(table, api.g_strdup("account"), api.g_strdup(request.account));
    return table;
}

void free_error(const secret::Api &api, secret::Error *&error) {
    if (error) {
        api.g_error_free(error);
        error = nullptr;
    }
}

int32_t report_error(const secret::Api &api, const secret::Error *error) {
    if (error && error->domain == api.g_io_error_quark() &&
        error->code == secret::io_error_permission_denied) {
        nkc::set_error("the operating-system credential store denied access");
        return NK_ERROR_PERMISSION_DENIED;
    }
    nkc::set_error(
        error && error->domain == api.secret_error_get_quark() &&
                error->code == secret::error_is_locked
            ? "the operating-system credential store is locked"
            : "the operating-system credential store is unavailable or rejected the request");
    return NK_ERROR_UNKNOWN;
}

} // namespace

namespace nkc {

int32_t set(const Request &request, const uint8_t *secret, uint32_t secret_size) {
    const auto *library = api();
    if (!library)
        return unavailable();
    auto *attrs = attributes(*library, request);
    auto *value = library->secret_value_new(reinterpret_cast<const char *>(secret), secret_size,
                                            "application/octet-stream");
    secret::Error *error = nullptr;
    const auto stored =
        library->secret_password_storev_binary_sync(schema(), attrs, secret::collection_default,
                                                    "NativeKit credential", value, nullptr, &error);
    library->secret_value_unref(value);
    library->g_hash_table_unref(attrs);
    if (!stored) {
        const auto result = report_error(*library, error);
        free_error(*library, error);
        return result;
    }
    free_error(*library, error);
    return NK_OK;
}

int32_t get(const Request &request, uint8_t *secret, uint32_t capacity, uint32_t *out_secret_size) {
    const auto *library = api();
    if (!library)
        return unavailable();
    auto *attrs = attributes(*library, request);
    secret::Error *error = nullptr;
    auto *value = library->secret_password_lookupv_binary_sync(schema(), attrs, nullptr, &error);
    library->g_hash_table_unref(attrs);
    if (error) {
        const auto result = report_error(*library, error);
        free_error(*library, error);
        return result;
    }
    if (!value) {
        set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    std::size_t value_size = 0;
    const auto *value_data =
        reinterpret_cast<const uint8_t *>(library->secret_value_get(value, &value_size));
    if (value_size > NK_CREDENTIALS_MAX_SECRET_BYTES) {
        library->secret_value_unref(value);
        set_error("stored credential exceeds the portable 2048-byte limit");
        return NK_ERROR_PAYLOAD_TOO_LARGE;
    }
    *out_secret_size = static_cast<uint32_t>(value_size);
    if (!secret || capacity < value_size) {
        library->secret_value_unref(value);
        set_error("credential output buffer is too small");
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(secret, value_data, value_size);
    library->secret_value_unref(value);
    return NK_OK;
}

int32_t erase(const Request &request) {
    const auto *library = api();
    if (!library)
        return unavailable();
    auto *attrs = attributes(*library, request);
    secret::Error *error = nullptr;
    const auto removed = library->secret_password_clearv_sync(schema(), attrs, nullptr, &error);
    library->g_hash_table_unref(attrs);
    if (error) {
        const auto result = report_error(*library, error);
        free_error(*library, error);
        return result;
    }
    if (!removed) {
        set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    return NK_OK;
}

} // namespace nkc
