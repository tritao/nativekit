#ifndef NATIVEKIT_H
#define NATIVEKIT_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(NK_STATIC)
#    define NK_API
#  elif defined(NK_BUILDING_LIBRARY)
#    define NK_API __declspec(dllexport)
#  else
#    define NK_API __declspec(dllimport)
#  endif
#  define NK_CALL __cdecl
#else
#  define NK_API __attribute__((visibility("default")))
#  define NK_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NK_API_VERSION 1u
#define NK_INVALID_HANDLE ((nk_handle)0)
#define NK_INVALID_REQUEST_ID ((nk_request_id)0)

typedef uint32_t nk_handle;
typedef uint64_t nk_request_id;
typedef int32_t nk_result;
typedef uint32_t nk_event_kind;

enum {
    NK_OK = 0,
    NK_ERROR_UNKNOWN = -1,
    NK_ERROR_INVALID_ARGUMENT = -2,
    NK_ERROR_INVALID_HANDLE = -3,
    NK_ERROR_UNSUPPORTED = -4,
    NK_ERROR_NOT_INITIALIZED = -5,
    NK_ERROR_ALREADY_INITIALIZED = -6,
    NK_ERROR_WRONG_THREAD = -7,
    NK_ERROR_OUT_OF_MEMORY = -8,
    NK_ERROR_QUEUE_FULL = -9,
    NK_ERROR_INVALID_REQUEST = -10,
    NK_ERROR_BUFFER_TOO_SMALL = -11
};

enum {
    NK_EVENT_NONE = 0,
    NK_EVENT_WINDOW_CLOSE = 1,
    NK_EVENT_WINDOW_RESIZE = 2,
    NK_EVENT_WINDOW_SCALE_CHANGED = 3,
    NK_EVENT_DIALOG_COMPLETE = 100,
    NK_EVENT_WEBVIEW_NAVIGATED = 200,
    NK_EVENT_WEBVIEW_MESSAGE = 201,
    NK_EVENT_WEBVIEW_TITLE_CHANGED = 202,
    NK_EVENT_WEBVIEW_EVAL_COMPLETE = 203,
    NK_EVENT_WEBVIEW_NAVIGATION_FAILED = 204,
    NK_EVENT_WEBVIEW_PROCESS_TERMINATED = 205,
    NK_EVENT_WEBVIEW_READY = 206,
    NK_EVENT_WEBVIEW_NAVIGATION_REQUEST = 207,
    NK_EVENT_DROP_FILES = 300,
    NK_EVENT_DROP_TEXT = 301,
    NK_EVENT_CLIPBOARD_TEXT_COMPLETE = 400,
    NK_EVENT_CLIPBOARD_FILES_COMPLETE = 401,
    NK_EVENT_NOTIFICATION_DELIVERED = 500,
    NK_EVENT_NOTIFICATION_ACTIVATED = 501,
    NK_EVENT_NOTIFICATION_DISMISSED = 502,
    NK_EVENT_NOTIFICATION_FAILED = 503
};

typedef struct nk_init_options {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t event_queue_capacity;
    uint32_t reserved;
} nk_init_options;

/*
 * Variable-length event data is owned by NativeKit. It remains valid until
 * nk_event_release() is called. A successfully polled event must be released,
 * even when data is NULL. Strings are UTF-8 and data_size excludes any trailing
 * NUL byte. Callers must zero-initialize this structure and set struct_size.
 */
typedef struct nk_event {
    uint32_t struct_size;
    nk_event_kind kind;
    nk_handle source;
    uint32_t flags;
    nk_request_id request_id;
    nk_result result;
    uint32_t data_count;
    const void *data;
    uint64_t data_size;
    uint64_t reserved[2];
} nk_event;

/* Returns the ABI version implemented by the loaded NativeKit library. */
NK_API uint32_t NK_CALL nk_api_version(void);

/*
 * Initializes NativeKit on the calling thread. That thread becomes the UI
 * thread until nk_shutdown(). Exactly one initialization may be active.
 */
NK_API nk_result NK_CALL nk_init(const nk_init_options *options);

/* Destroys all remaining resources. Call on the UI thread. */
NK_API void NK_CALL nk_shutdown(void);

/*
 * Returns a thread-local UTF-8 diagnostic for the most recent failing call.
 * The pointer remains valid until the next NativeKit call on this thread.
 */
NK_API const char *NK_CALL nk_last_error(void);

/*
 * Polls one event on the UI thread. Returns NK_OK with NK_EVENT_NONE when the
 * queue is empty. `event` must be zero-initialized with struct_size set.
 */
NK_API nk_result NK_CALL nk_poll_event(nk_event *event);

/* Releases an event returned by nk_poll_event; safe for an empty event. */
NK_API void NK_CALL nk_event_release(nk_event *event);

#ifdef __cplusplus
}
#endif

#endif
