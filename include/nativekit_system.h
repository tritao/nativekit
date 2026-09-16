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

enum NK_ENUM(nk_system_directory_kind) {
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
    NK_DIRECTORY_TEMP = 8,
    /** Read-only installation or bundle directory, when path-backed. */
    NK_DIRECTORY_APPLICATION = 9,
    /** Private, persistent, application-specific writable storage. */
    NK_DIRECTORY_APPLICATION_STORAGE = 10,
    /** Best-known system font directory on desktop platforms. */
    NK_DIRECTORY_FONTS = 11
};

/** Stable platform identity reported by nk_system_get_info(). */
typedef uint32_t nk_system_platform;

enum NK_ENUM(nk_system_platform) {
    NK_SYSTEM_PLATFORM_UNKNOWN = 0,
    NK_SYSTEM_PLATFORM_LINUX = 1,
    NK_SYSTEM_PLATFORM_WINDOWS = 2,
    NK_SYSTEM_PLATFORM_MACOS = 3,
    NK_SYSTEM_PLATFORM_ANDROID = 4,
    NK_SYSTEM_PLATFORM_IOS = 5,
    NK_SYSTEM_PLATFORM_WEB = 6
};

/** Native byte order of the running platform. */
typedef uint32_t nk_system_endianness;

enum NK_ENUM(nk_system_endianness) {
    NK_SYSTEM_ENDIAN_UNKNOWN = 0,
    NK_SYSTEM_ENDIAN_LITTLE = 1,
    NK_SYSTEM_ENDIAN_BIG = 2
};

/** Compact platform and device identity information. */
typedef struct nk_system_info {
    /** Set to sizeof(nk_system_info) before the query. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Stable NativeKit platform identifier. */
    nk_system_platform platform;
    /** Native byte order. */
    nk_system_endianness endianness;
    /** Non-zero for mobile platforms. */
    nk_bool mobile;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future system information; set all elements to zero. */
    uint64_t reserved2[2];
} nk_system_info;

/** String values accepted by nk_system_get_string(). */
typedef uint32_t nk_system_string_kind;

enum NK_ENUM(nk_system_string_kind) {
    NK_SYSTEM_STRING_PLATFORM_NAME = 1,
    NK_SYSTEM_STRING_PLATFORM_VERSION = 2,
    NK_SYSTEM_STRING_PLATFORM_LABEL = 3,
    NK_SYSTEM_STRING_DEVICE_VENDOR = 4,
    NK_SYSTEM_STRING_DEVICE_MODEL = 5,
    NK_SYSTEM_STRING_APPLICATION_ID = 6,
    NK_SYSTEM_STRING_APPLICATION_NAME = 7
};

/** Reasons and options for an ownership-safe keep-awake lease. */
typedef uint32_t nk_keep_awake_flags;

enum NK_FLAGS(nk_keep_awake_flags) {
    /** Keep the application display awake while the lease is held. */
    NK_KEEP_AWAKE_DISPLAY = 1u << 0
};

typedef uint32_t nk_keep_awake;

typedef struct nk_keep_awake_options {
    /** Set to sizeof(nk_keep_awake_options) before acquiring a lease. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Optional diagnostic reason copied only for the duration of the call. */
    const char *reason NK_NULLABLE_UTF8;
    /** Requested keep-awake facilities. */
    nk_keep_awake_flags flags;
    /** Reserved for future lease options; set all elements to zero. */
    uint64_t reserved[2];
} nk_keep_awake_options;

/** Physical and presentation orientations. */
typedef uint32_t nk_orientation;

enum NK_ENUM(nk_orientation) {
    NK_ORIENTATION_UNKNOWN = 0,
    NK_ORIENTATION_PORTRAIT = 1,
    NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN = 2,
    NK_ORIENTATION_LANDSCAPE_LEFT = 3,
    NK_ORIENTATION_LANDSCAPE_RIGHT = 4,
    NK_ORIENTATION_FACE_UP = 5,
    NK_ORIENTATION_FACE_DOWN = 6
};

/** Current physical device posture and application display orientation. */
typedef struct nk_system_orientation {
    /** Set to sizeof(nk_system_orientation) before the query. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Physical device posture, when the platform reports it. */
    nk_orientation device;
    /** Orientation used to present application content. */
    nk_orientation display;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future orientation information; set all elements to zero. */
    uint64_t reserved2[2];
} nk_system_orientation;

/** Payload shared by device and display orientation events. */
typedef struct nk_orientation_event {
    /** Set to sizeof(nk_orientation_event). */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** New orientation value. */
    nk_orientation orientation;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future orientation event information; set all elements to zero. */
    uint64_t reserved2[2];
} nk_orientation_event;

enum NK_ENUM(nk_color_scheme) {
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
    uint32_t struct_size NK_STRUCT_SIZE;
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

/**
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

/**
 * Writes a NUL-terminated UTF-8 value. `inout_size` is the buffer capacity on
 * entry and receives the required size including NUL on every valid call. Pass
 * NULL as `buffer` to query the size. An absent or small buffer returns
 * NK_ERROR_BUFFER_TOO_SMALL without truncating the value.
 */
NK_API nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                             uint32_t *inout_size);
/** Copies the current system locale identifier into a caller-owned UTF-8 buffer. */
NK_API nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size);

/** Returns stable platform identity and native endianness. */
NK_API nk_result NK_CALL nk_system_get_info(nk_system_info *out_info NK_OUT);
/** Copies a privacy-safe platform, device, or configured application string. */
NK_API nk_result NK_CALL nk_system_get_string(nk_system_string_kind kind, char *buffer,
                                              uint32_t *inout_size);
/** Acquires an independent keep-awake lease. */
NK_API nk_result NK_CALL nk_system_keep_awake_acquire(const nk_keep_awake_options *options,
                                                      nk_keep_awake *out_lock NK_OUT);
/** Releases a previously acquired keep-awake lease. */
NK_API nk_result NK_CALL nk_system_keep_awake_release(nk_keep_awake lock);
/** Returns physical device and application display orientation. */
NK_API nk_result NK_CALL nk_system_get_orientation(nk_system_orientation *out_orientation NK_OUT);

/** Returns the current platform appearance. UI thread only. */
NK_API nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance);

#ifdef __cplusplus
}
#endif

#endif
