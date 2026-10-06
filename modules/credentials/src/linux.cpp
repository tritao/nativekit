#include "backend.hpp"

#include <libsecret/secret.h>

#include <cstring>

namespace {

const SecretSchema *schema() {
    static const SecretSchema value = {
        "org.nativekit.Credential",
        SECRET_SCHEMA_NONE,
        {{"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
         {nullptr, static_cast<SecretSchemaAttributeType>(0)}}};
    return &value;
}

GHashTable *attributes(const nkc::Request &request) {
    auto *table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(table, g_strdup("service"), g_strdup(request.service));
    g_hash_table_insert(table, g_strdup("account"), g_strdup(request.account));
    return table;
}

int32_t report_error(GError *error) {
    if (error && error->domain == G_IO_ERROR && error->code == G_IO_ERROR_PERMISSION_DENIED) {
        nkc::set_error("the operating-system credential store denied access");
        return NK_ERROR_PERMISSION_DENIED;
    }
    nkc::set_error(error && error->domain == SECRET_ERROR && error->code == SECRET_ERROR_IS_LOCKED
                       ? "the operating-system credential store is locked"
                       : "the operating-system credential store is unavailable or rejected the request");
    return NK_ERROR_UNKNOWN;
}

} // namespace

namespace nkc {

int32_t set(const Request &request, const uint8_t *secret, uint32_t secret_size) {
    auto *attrs = attributes(request);
    auto *value = secret_value_new(reinterpret_cast<const gchar *>(secret), secret_size,
                                   "application/octet-stream");
    GError *error = nullptr;
    const gboolean stored = secret_password_storev_binary_sync(
        schema(), attrs, SECRET_COLLECTION_DEFAULT, "NativeKit credential", value, nullptr, &error);
    secret_value_unref(value);
    g_hash_table_unref(attrs);
    if (!stored) {
        const auto result = report_error(error);
        g_clear_error(&error);
        return result;
    }
    g_clear_error(&error);
    return NK_OK;
}

int32_t get(const Request &request, uint8_t *secret, uint32_t capacity,
            uint32_t *out_secret_size) {
    auto *attrs = attributes(request);
    GError *error = nullptr;
    auto *value = secret_password_lookupv_binary_sync(schema(), attrs, nullptr, &error);
    g_hash_table_unref(attrs);
    if (error) {
        const auto result = report_error(error);
        g_clear_error(&error);
        return result;
    }
    if (!value) {
        set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    gsize value_size = 0;
    const auto *value_data = reinterpret_cast<const uint8_t *>(secret_value_get(value, &value_size));
    if (value_size > NK_CREDENTIALS_MAX_SECRET_BYTES) {
        secret_value_unref(value);
        set_error("stored credential exceeds the portable 2048-byte limit");
        return NK_ERROR_PAYLOAD_TOO_LARGE;
    }
    *out_secret_size = static_cast<uint32_t>(value_size);
    if (!secret || capacity < value_size) {
        secret_value_unref(value);
        set_error("credential output buffer is too small");
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(secret, value_data, value_size);
    secret_value_unref(value);
    return NK_OK;
}

int32_t erase(const Request &request) {
    auto *attrs = attributes(request);
    GError *error = nullptr;
    const gboolean removed = secret_password_clearv_sync(schema(), attrs, nullptr, &error);
    g_hash_table_unref(attrs);
    if (error) {
        const auto result = report_error(error);
        g_clear_error(&error);
        return result;
    }
    if (!removed) {
        set_error("credential was not found");
        return NK_ERROR_NOT_FOUND;
    }
    return NK_OK;
}

} // namespace nkc
