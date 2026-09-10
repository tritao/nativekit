#ifndef NATIVEKIT_H
#define NATIVEKIT_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Export and calling convention                                             */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NK_API
#elif defined(NK_BUILDING_LIBRARY)
#define NK_API __declspec(dllexport)
#else
#define NK_API __declspec(dllimport)
#endif
#define NK_CALL __cdecl
#else
#define NK_API __attribute__((visibility("default")))
#define NK_CALL
#endif

/* ------------------------------------------------------------------------- */
/* Binding annotations                                                       */
/* ------------------------------------------------------------------------- */

/* Semantic FFI directions. They are no-ops outside Clang-based binding import. */
#if defined(__clang__)
#define NK_OUT __attribute__((annotate("hxi:out")))
#define NK_INOUT __attribute__((annotate("hxi:inout")))
#define NK_OUT_BUFFER(size_parameter) __attribute__((annotate("hxi:out_buffer")))
#define NK_IN_ARRAY(count_parameter) __attribute__((annotate("hxi:in_array")))
#define NK_IN_UTF8_ARRAY(count_parameter)                                                        \
    __attribute__((annotate("hxi:in_array"))) __attribute__((annotate("hxi:utf8_array")))
#define NK_RETURNS_BORROWED_UTF8 __attribute__((annotate("hxi:returns_borrowed_utf8")))
#define NK_UTF8 __attribute__((annotate("hxi:utf8")))
#define NK_NULLABLE_UTF8 __attribute__((annotate("hxi:nullable_utf8")))
#define NK_BORROWED_BUFFER(length_field)                                                          \
    __attribute__((annotate("hxi:borrowed"))) __attribute__((annotate("hxi:length_field")))
#define NK_BORROWED_ARRAY(count_field)                                                             \
    __attribute__((annotate("hxi:borrowed"))) __attribute__((annotate("hxi:length_field")))
#else
#define NK_OUT
#define NK_INOUT
#define NK_OUT_BUFFER(size_parameter)
#define NK_IN_ARRAY(count_parameter)
#define NK_IN_UTF8_ARRAY(count_parameter)
#define NK_RETURNS_BORROWED_UTF8
#define NK_UTF8
#define NK_NULLABLE_UTF8
#define NK_BORROWED_BUFFER(length_field)
#define NK_BORROWED_ARRAY(count_field)
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Core identifiers and types                                                */
/* ------------------------------------------------------------------------- */

enum {
    /** Version of the NativeKit C ABI described by these headers. */
    NK_API_VERSION = 2
};

/** Sentinel for an invalid, destroyed, or otherwise unavailable resource. */
#define NK_INVALID_HANDLE ((nk_handle)0)

/** Sentinel meaning that an asynchronous request ID is not present. */
#define NK_INVALID_REQUEST_ID ((nk_request_id)0)

/** Opaque generation-checked identifier for a live NativeKit resource. */
typedef uint32_t nk_handle;

/** Identifier for one asynchronous operation; it is not a resource handle. */
typedef uint64_t nk_request_id;

/** Signed result returned by NativeKit operations. */
typedef int32_t nk_result;

/** Numeric kind identifying the payload and source of an event. */
typedef uint32_t nk_event_kind;

/**
 * Boolean value used by the NativeKit ABI. Zero is false; non-zero input is
 * true; NativeKit normalizes boolean outputs to zero or one.
 */
typedef uint32_t nk_bool;

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

/*
 * All failures have a thread-local diagnostic available from nk_last_error().
 * INVALID_ARGUMENT describes caller data; INVALID_HANDLE describes a stale or
 * wrong-kind resource; INVALID_REQUEST describes a valid operation that can no
 * longer complete in its current lifecycle state.
 */
enum {
    /** Operation completed successfully. */
    NK_OK = 0,
    /** The backend encountered an unspecified failure. */
    NK_ERROR_UNKNOWN = -1,
    /** A caller-provided pointer, value, size, or combination is invalid. */
    NK_ERROR_INVALID_ARGUMENT = -2,
    /** A handle is zero, stale, or belongs to a different resource kind. */
    NK_ERROR_INVALID_HANDLE = -3,
    /** The requested feature or operation is unavailable on this backend. */
    NK_ERROR_UNSUPPORTED = -4,
    /** NativeKit has not been initialized on the calling thread. */
    NK_ERROR_NOT_INITIALIZED = -5,
    /** NativeKit is already initialized by another active runtime. */
    NK_ERROR_ALREADY_INITIALIZED = -6,
    /** An operation that requires the UI thread was called elsewhere. */
    NK_ERROR_WRONG_THREAD = -7,
    /** The operation could not allocate the required memory. */
    NK_ERROR_OUT_OF_MEMORY = -8,
    /** A non-terminal event could not be added because the queue is full. */
    NK_ERROR_QUEUE_FULL = -9,
    /** A valid request can no longer complete in its current lifecycle state. */
    NK_ERROR_INVALID_REQUEST = -10,
    /** A caller-provided output buffer is too small for the requested result. */
    NK_ERROR_BUFFER_TOO_SMALL = -11
};

/* ------------------------------------------------------------------------- */
/* Event kinds                                                               */
/* ------------------------------------------------------------------------- */

enum {
    /** No event was available when the queue was polled. */
    NK_EVENT_NONE = 0,
    /** The user requested that a window close. */
    NK_EVENT_WINDOW_CLOSE = 1,
    /** The logical content size of a window changed. */
    NK_EVENT_WINDOW_RESIZE = 2,
    /** The content-to-device pixel scale of a window changed. */
    NK_EVENT_WINDOW_SCALE_CHANGED = 3,
    /** The native window state flags changed. */
    NK_EVENT_WINDOW_STATE_CHANGED = 4,
    /** The logical position of a window changed. */
    NK_EVENT_WINDOW_MOVE = 5,
    /** The device-pixel framebuffer size of a window changed. */
    NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE = 6,
    /** A keyboard key transitioned or repeated. */
    NK_EVENT_KEY = 10,
    /** Committed Unicode text was entered. */
    NK_EVENT_TEXT_INPUT = 11,
    /** The pointer moved within a window or graphics surface. */
    NK_EVENT_POINTER_MOVE = 12,
    /** A pointer button transitioned. */
    NK_EVENT_POINTER_BUTTON = 13,
    /** The pointer wheel or touchpad scroll position changed. */
    NK_EVENT_POINTER_SCROLL = 14,
    /** The pointer entered or left a window or graphics surface. */
    NK_EVENT_POINTER_ENTER = 15,
    /** A touch contact was created, moved, or released. */
    NK_EVENT_TOUCH = 16,
    /** A structured text-edit transaction was produced by an IME. */
    NK_EVENT_TEXT_EDIT = 17,
    /** A monitor became available. */
    NK_EVENT_MONITOR_CONNECTED = 20,
    /** A monitor was disconnected and its handle invalidated. */
    NK_EVENT_MONITOR_DISCONNECTED = 21,
    /** A raw joystick became available. */
    NK_EVENT_JOYSTICK_CONNECTED = 30,
    /** A raw joystick was disconnected and its handle invalidated. */
    NK_EVENT_JOYSTICK_DISCONNECTED = 31,
    /** A raw joystick axis changed. */
    NK_EVENT_JOYSTICK_AXIS = 32,
    /** A raw joystick button changed. */
    NK_EVENT_JOYSTICK_BUTTON = 33,
    /** A raw joystick hat changed. */
    NK_EVENT_JOYSTICK_HAT = 34,
    /** A mapped gamepad axis changed. */
    NK_EVENT_GAMEPAD_AXIS = 35,
    /** A mapped gamepad button changed. */
    NK_EVENT_GAMEPAD_BUTTON = 36,
    /** An asynchronous file or directory dialog returned paths. */
    NK_EVENT_DIALOG_PATHS_COMPLETE = 100,
    /** An asynchronous resource dialog returned resource URIs. */
    NK_EVENT_DIALOG_RESOURCES_COMPLETE = 101,
    /** An asynchronous message dialog returned a button result. */
    NK_EVENT_DIALOG_MESSAGE_COMPLETE = 102,
    /** A WebView completed a navigation. */
    NK_EVENT_WEBVIEW_NAVIGATED = 200,
    /** A WebView delivered a page-to-native message. */
    NK_EVENT_WEBVIEW_MESSAGE = 201,
    /** A WebView page title changed. */
    NK_EVENT_WEBVIEW_TITLE_CHANGED = 202,
    /** A WebView JavaScript evaluation completed. */
    NK_EVENT_WEBVIEW_EVAL_COMPLETE = 203,
    /** A WebView navigation failed. */
    NK_EVENT_WEBVIEW_NAVIGATION_FAILED = 204,
    /** A WebView rendering process terminated and its handle was invalidated. */
    NK_EVENT_WEBVIEW_PROCESS_TERMINATED = 205,
    /** A WebView finished asynchronous creation. */
    NK_EVENT_WEBVIEW_READY = 206,
    /** A WebView navigation requires an application allow/cancel decision. */
    NK_EVENT_WEBVIEW_NAVIGATION_REQUEST = 207,
    /** Local files were dropped on a window. */
    NK_EVENT_DROP_FILES = 300,
    /** Text was dropped on a window. */
    NK_EVENT_DROP_TEXT = 301,
    /** An asynchronous clipboard text read completed. */
    NK_EVENT_CLIPBOARD_TEXT_COMPLETE = 400,
    /** An asynchronous clipboard file read completed. */
    NK_EVENT_CLIPBOARD_FILES_COMPLETE = 401,
    /** An asynchronous clipboard resource read completed. */
    NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE = 402,
    /** A notification was delivered by the platform. */
    NK_EVENT_NOTIFICATION_DELIVERED = 500,
    /** A user activated a notification. */
    NK_EVENT_NOTIFICATION_ACTIVATED = 501,
    /** A notification was dismissed. */
    NK_EVENT_NOTIFICATION_DISMISSED = 502,
    /** A notification could not be shown or completed. */
    NK_EVENT_NOTIFICATION_FAILED = 503,
    /** The attached mobile host geometry or insets changed. */
    NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED = 600,
    /** A graphics surface became ready for rendering. */
    NK_EVENT_SURFACE_READY = 700,
    /** A graphics surface framebuffer changed size. */
    NK_EVENT_SURFACE_RESIZE = 701,
    /** A graphics surface became unavailable. */
    NK_EVENT_SURFACE_LOST = 702,
    /** A custom-surface accessibility action was requested. */
    NK_EVENT_ACCESSIBILITY_ACTION = 703,
    /** A mobile host opened one or more resources. */
    NK_EVENT_RESOURCE_OPENED = 800,
    /** A mobile host received a share operation. */
    NK_EVENT_SHARE_RECEIVED = 801,
    /** A mobile host received dropped resources or text. */
    NK_EVENT_RESOURCE_DROP = 802
};

/* ------------------------------------------------------------------------- */
/* Initialization and event data                                             */
/* ------------------------------------------------------------------------- */

/** Options used to start one NativeKit runtime generation. */
typedef struct nk_init_options {
    /** Size of this structure in bytes; must be at least sizeof(nk_init_options). */
    uint32_t struct_size;
    /** ABI version requested by the caller; normally NK_API_VERSION. */
    uint32_t api_version;
    /** Event queue capacity; zero selects the backend default. */
    uint32_t event_queue_capacity;
    /** Reserved for compatible extensions; initialize to zero. */
    uint32_t reserved;
} nk_init_options;

/**
 * Variable-length event data is owned by NativeKit. It remains valid until
 * nk_event_release() is called. A successfully polled event must be released,
 * even when data is NULL. Strings are UTF-8 and data_size excludes any trailing
 * NUL byte. Callers must zero-initialize this structure and set struct_size.
 */
typedef struct nk_event {
    /** Size of this structure in bytes; must be set before polling. */
    uint32_t struct_size;
    /** Event kind; use this to select the payload interpretation. */
    nk_event_kind kind;
    /** Originating resource handle, or NK_INVALID_HANDLE for global events. */
    nk_handle source;
    /** Event-specific flags; zero when the event kind defines no flags. */
    uint32_t flags;
    /** Related asynchronous request, or NK_INVALID_REQUEST_ID when absent. */
    nk_request_id request_id;
    /** Result associated with a completion event; usually NK_OK on success. */
    nk_result result;
    /** Number of logical records in data for payloads that contain an array. */
    uint32_t data_count;
    /** Borrowed pointer to event-owned payload bytes; valid until release. */
    const void *data NK_BORROWED_BUFFER(data_size);
    /** Payload size in bytes, excluding any trailing NUL byte. */
    uint64_t data_size;
    /** Reserved for compatible extensions; initialize to zero. */
    uint64_t reserved[2];
} nk_event;

/* ------------------------------------------------------------------------- */
/* Lifecycle and diagnostics                                                 */
/* ------------------------------------------------------------------------- */

/** Returns the ABI version implemented by the loaded NativeKit library. */
NK_API uint32_t NK_CALL nk_api_version(void);

/**
 * Initializes NativeKit on the calling thread. That thread becomes the UI
 * thread until nk_shutdown(). Exactly one initialization may be active.
 *
 * @param options Non-null, zero-initialized options with struct_size and
 *                api_version set.
 * @return NK_OK, or an error describing invalid options, an unsupported
 *         version, an existing runtime, or allocation failure.
 */
NK_API nk_result NK_CALL nk_init(const nk_init_options *options);

/**
 * Destroys all remaining resources and ends the active runtime generation.
 * Call on the UI thread after releasing any event currently held by the caller.
 */
NK_API void NK_CALL nk_shutdown(void);

/**
 * Returns a thread-local UTF-8 diagnostic for the most recent failing call.
 * The pointer remains valid until the next NativeKit call on this thread.
 * The returned string may be empty after a successful call.
 */
NK_API const char *NK_CALL nk_last_error(void) NK_RETURNS_BORROWED_UTF8;

/* ------------------------------------------------------------------------- */
/* Event queue                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Polls one event on the UI thread. Returns NK_OK with NK_EVENT_NONE when the
 * queue is empty. `event` must be zero-initialized with struct_size set.
 * A successfully polled event, including NK_EVENT_NONE, must be released with
 * nk_event_release() before the structure is reused.
 *
 * @param event Output event structure. Its data pointer must be NULL before
 *              the call.
 * @return NK_OK for an event or an empty queue, or an error if event is invalid
 *         or the call is made from the wrong lifecycle/thread state.
 */
NK_API nk_result NK_CALL nk_poll_event(nk_event *event NK_INOUT);

/**
 * Releases payload storage returned by nk_poll_event and clears the event.
 * Safe to call with NULL or an empty event; struct_size is preserved for reuse.
 */
NK_API void NK_CALL nk_event_release(nk_event *event);

#ifdef __cplusplus
}
#endif

#endif
