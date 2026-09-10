#ifndef NATIVEKIT_SYSTEM_H
#define NATIVEKIT_SYSTEM_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* System types and constants                                                */
/* ------------------------------------------------------------------------- */

/** Well-known directories returned by nk_system_directory(). */
typedef uint32_t nk_system_directory_kind;
/** System color-scheme values returned in nk_system_appearance. */
typedef uint32_t nk_color_scheme;

enum {
    /** User home directory. */
    NK_DIRECTORY_HOME = 1,
    /** User desktop directory. */
    NK_DIRECTORY_DESKTOP = 2,
    /** User documents directory. */
    NK_DIRECTORY_DOCUMENTS = 3,
    /** User downloads directory. */
    NK_DIRECTORY_DOWNLOADS = 4,
    /** Application cache directory. */
    NK_DIRECTORY_CACHE = 5,
    /** Application configuration directory. */
    NK_DIRECTORY_CONFIG = 6,
    /** Application data directory. */
    NK_DIRECTORY_DATA = 7,
    /** Temporary-file directory. */
    NK_DIRECTORY_TEMP = 8
};

enum {
    /** The platform did not report a color scheme. */
    NK_COLOR_SCHEME_UNKNOWN = 0,
    /** The platform is using a light color scheme. */
    NK_COLOR_SCHEME_LIGHT = 1,
    /** The platform is using a dark color scheme. */
    NK_COLOR_SCHEME_DARK = 2
};

/** Current system appearance reported by the attached platform. */
typedef struct nk_system_appearance {
    /** Set to sizeof(nk_system_appearance) before the query. */
    uint32_t struct_size;
    /** Current platform color scheme. */
    nk_color_scheme color_scheme;
    /** Non-zero when the platform's high-contrast mode is enabled. */
    uint32_t high_contrast;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future appearance information; set all elements to zero. */
    uint64_t reserved2[2];
} nk_system_appearance;

/* ------------------------------------------------------------------------- */
/* Shell operations                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Requests that the desktop open a URI, open a local file with its default
 * application, or reveal a local file in its containing folder. Input is
 * copied before return. These functions must be called on the UI thread.
 */
NK_API nk_result NK_CALL nk_shell_open_url(const char *url NK_UTF8);
/** Opens a local file with the platform's default application. */
NK_API nk_result NK_CALL nk_shell_open_file(const char *path NK_UTF8);
/** Reveals a local file in its containing folder. */
NK_API nk_result NK_CALL nk_shell_reveal_file(const char *path NK_UTF8);

/* ------------------------------------------------------------------------- */
/* System queries                                                            */
/* ------------------------------------------------------------------------- */

/*
 * Writes a NUL-terminated UTF-8 value. `inout_size` is the buffer capacity on
 * entry and receives the required size including NUL on every valid call. Pass
 * NULL as `buffer` to query the size. An absent or small buffer returns
 * NK_ERROR_BUFFER_TOO_SMALL without truncating the value.
 */
NK_API nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                             uint32_t *inout_size);
/** Copies the current system locale identifier into a caller-owned UTF-8 buffer. */
NK_API nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size);

/* Returns the current desktop appearance. UI thread only. */
NK_API nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance);

#ifdef __cplusplus
}
#endif

#endif
