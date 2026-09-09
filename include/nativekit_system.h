#ifndef NATIVEKIT_SYSTEM_H
#define NATIVEKIT_SYSTEM_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_system_directory_kind;
typedef uint32_t nk_color_scheme;

enum {
    NK_DIRECTORY_HOME = 1,
    NK_DIRECTORY_DESKTOP = 2,
    NK_DIRECTORY_DOCUMENTS = 3,
    NK_DIRECTORY_DOWNLOADS = 4,
    NK_DIRECTORY_CACHE = 5,
    NK_DIRECTORY_CONFIG = 6,
    NK_DIRECTORY_DATA = 7,
    NK_DIRECTORY_TEMP = 8
};

enum {
    NK_COLOR_SCHEME_UNKNOWN = 0,
    NK_COLOR_SCHEME_LIGHT = 1,
    NK_COLOR_SCHEME_DARK = 2
};

typedef struct nk_system_appearance {
    uint32_t struct_size;
    nk_color_scheme color_scheme;
    uint32_t high_contrast;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_system_appearance;

/*
 * Requests that the desktop open a URI, open a local file with its default
 * application, or reveal a local file in its containing folder. Input is
 * copied before return. These functions must be called on the UI thread.
 */
NK_API nk_result NK_CALL nk_shell_open_url(const char *url);
NK_API nk_result NK_CALL nk_shell_open_file(const char *path);
NK_API nk_result NK_CALL nk_shell_reveal_file(const char *path);

/*
 * Writes a NUL-terminated UTF-8 value. `inout_size` is the buffer capacity on
 * entry and receives the required size including NUL on every valid call. Pass
 * NULL as `buffer` to query the size. An absent or small buffer returns
 * NK_ERROR_BUFFER_TOO_SMALL without truncating the value.
 */
NK_API nk_result NK_CALL nk_system_directory(
    nk_system_directory_kind kind, char *buffer, uint32_t *inout_size);
NK_API nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size);

/* Returns the current desktop appearance. UI thread only. */
NK_API nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance);

#ifdef __cplusplus
}
#endif

#endif
