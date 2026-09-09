/*
 * Standard-path and appearance behavior is informed by wxWidgets
 * src/common/stdpbase.cpp and src/gtk/settings.cpp at the revision recorded in
 * tools/upstream-lock.json. See licenses/wxWidgets.txt.
 */

#include "nativekit_system.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace {

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

nk_result copy_utf8(const char* value, char* buffer, uint32_t* inout_size) {
    if (!value || !inout_size)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid string output arguments");
    const auto length = std::strlen(value);
    if (length >= std::numeric_limits<uint32_t>::max())
        return fail(NK_ERROR_UNKNOWN, "system string is too large");
    const auto required = static_cast<uint32_t>(length + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "output buffer is too small");
    std::memcpy(buffer, value, required);
    return NK_OK;
}

nk_result launch_uri(const char* uri) {
    GError* error = nullptr;
    const gboolean launched = g_app_info_launch_default_for_uri(uri, nullptr, &error);
    if (!launched) {
        nk::core::set_error(error && error->message
            ? error->message : "desktop could not launch the URI");
        if (error) g_error_free(error);
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result open_path(const char* path) {
    if (!path || !*path) return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    char* absolute = g_canonicalize_filename(path, nullptr);
    GError* error = nullptr;
    char* uri = g_filename_to_uri(absolute, nullptr, &error);
    g_free(absolute);
    if (!uri) {
        nk::core::set_error(error && error->message ? error->message : "invalid file path");
        if (error) g_error_free(error);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto result = launch_uri(uri);
    g_free(uri);
    return result;
}

const char* directory(nk_system_directory_kind kind) {
    switch (kind) {
        case NK_DIRECTORY_HOME: return g_get_home_dir();
        case NK_DIRECTORY_DESKTOP: return g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
        case NK_DIRECTORY_DOCUMENTS: return g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
        case NK_DIRECTORY_DOWNLOADS: return g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
        case NK_DIRECTORY_CACHE: return g_get_user_cache_dir();
        case NK_DIRECTORY_CONFIG: return g_get_user_config_dir();
        case NK_DIRECTORY_DATA: return g_get_user_data_dir();
        case NK_DIRECTORY_TEMP: return g_get_tmp_dir();
        default: return nullptr;
    }
}

}

extern "C" {

nk_result NK_CALL nk_shell_open_url(const char* url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!url || !*url)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a URI scheme");
    char* scheme = g_uri_parse_scheme(url);
    if (!scheme) return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a URI scheme");
    g_free(scheme);
    return launch_uri(url);
}

nk_result NK_CALL nk_shell_open_file(const char* path) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    return open_path(path);
}

nk_result NK_CALL nk_shell_reveal_file(const char* path) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!path || !*path) return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    char* absolute = g_canonicalize_filename(path, nullptr);
    GError* error = nullptr;
    char* uri = g_filename_to_uri(absolute, nullptr, &error);
    if (!uri) {
        g_free(absolute);
        nk::core::set_error(error && error->message ? error->message : "invalid file path");
        if (error) g_error_free(error);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    bool revealed = false;
    if (bus) {
        GVariantBuilder uris;
        g_variant_builder_init(&uris, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&uris, "s", uri);
        GVariant* reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1", "ShowItems",
            g_variant_new("(ass)", &uris, ""), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        revealed = reply != nullptr;
        if (reply) g_variant_unref(reply);
        g_object_unref(bus);
    }
    g_free(uri);
    if (revealed) {
        g_free(absolute);
        return NK_OK;
    }
    char* parent = g_path_get_dirname(absolute);
    g_free(absolute);
    const auto fallback = open_path(parent);
    g_free(parent);
    return fallback;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind,
                                      char* buffer, uint32_t* inout_size) {
    nk::core::clear_error();
    const char* value = directory(kind);
    if (!value) return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    return copy_utf8(value, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char* buffer, uint32_t* inout_size) {
    nk::core::clear_error();
    const char* const* languages = g_get_language_names();
    if (!languages || !languages[0])
        return fail(NK_ERROR_UNSUPPORTED, "system locale is unavailable");
    return copy_utf8(languages[0], buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance* appearance) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return fail(NK_ERROR_INVALID_ARGUMENT, "appearance output is missing or too small");
    int argc = 0;
    char** argv = nullptr;
    if (!gtk_init_check(&argc, &argv))
        return fail(NK_ERROR_UNSUPPORTED, "GTK could not connect to a display");
    GtkSettings* settings = gtk_settings_get_default();
    if (!settings) return fail(NK_ERROR_UNSUPPORTED, "desktop appearance is unavailable");
    gboolean prefer_dark = FALSE;
    char* theme = nullptr;
    g_object_get(settings,
                 "gtk-application-prefer-dark-theme", &prefer_dark,
                 "gtk-theme-name", &theme,
                 nullptr);
    char* normalized = g_ascii_strdown(theme ? theme : "", -1);
    appearance->color_scheme = prefer_dark || std::strstr(normalized, "dark")
        ? NK_COLOR_SCHEME_DARK : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast = std::strstr(normalized, "highcontrast") ||
                                std::strstr(normalized, "high-contrast");
    g_free(normalized);
    g_free(theme);
    return NK_OK;
}

}
