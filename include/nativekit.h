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
#define NK_RETAINED __attribute__((annotate("hxi:retained")))
#define NK_IN_UTF8_ARRAY(count_parameter)                                                          \
    __attribute__((annotate("hxi:in_array"))) __attribute__((annotate("hxi:utf8_array")))
#define NK_OUT_UTF8_ARRAY(count_parameter)                                                         \
    __attribute__((annotate("hxi:out_array"))) __attribute__((annotate("hxi:utf8_array")))
#define NK_RETURNS_BORROWED_UTF8 __attribute__((annotate("hxi:returns_borrowed_utf8")))
#define NK_UTF8 __attribute__((annotate("hxi:utf8")))
#define NK_NULLABLE_UTF8 __attribute__((annotate("hxi:nullable_utf8")))
#define NK_BORROWED_BUFFER(length_field)                                                           \
    __attribute__((annotate("hxi:borrowed"))) __attribute__((annotate("hxi:length_field")))
#define NK_BORROWED_ARRAY(count_field)                                                             \
    __attribute__((annotate("hxi:borrowed"))) __attribute__((annotate("hxi:length_field")))
#define NK_HANDLE __attribute__((annotate("hxi:handle")))
#define NK_HANDLE_DESTROY(symbol) __attribute__((annotate("hxi:handle_destroy")))
#define NK_OWNED __attribute__((annotate("hxi:owned")))
#define NK_BOOL32 __attribute__((annotate("hxi:bool32")))
#define NK_STRUCT_SIZE __attribute__((annotate("hxi:struct_size")))
#define NK_NULLABLE _Nullable
/* Keeps the C typedef as the ABI source of truth while naming the enum and its HXI projection. */
#define NK_ENUM(name) __attribute__((annotate("hxi:enum:" #name))) name##_enum
/* Marks an integer typedef and its constants as a bitmask without changing C ABI storage. */
#define NK_FLAGS(name) __attribute__((annotate("hxi:flags:" #name))) name##_flags_enum
#else
#define NK_OUT
#define NK_INOUT
#define NK_OUT_BUFFER(size_parameter)
#define NK_IN_ARRAY(count_parameter)
#define NK_RETAINED
#define NK_IN_UTF8_ARRAY(count_parameter)
#define NK_OUT_UTF8_ARRAY(count_parameter)
#define NK_RETURNS_BORROWED_UTF8
#define NK_UTF8
#define NK_NULLABLE_UTF8
#define NK_BORROWED_BUFFER(length_field)
#define NK_BORROWED_ARRAY(count_field)
#define NK_HANDLE
#define NK_HANDLE_DESTROY(symbol)
#define NK_OWNED
#define NK_BOOL32
#define NK_STRUCT_SIZE
#define NK_NULLABLE
#define NK_ENUM(name) name##_enum
#define NK_FLAGS(name) name##_flags_enum
#endif

/* Every named NativeKit handle is a value-copyable, four-byte opaque token. */
#define NK_DECLARE_HANDLE(name)                                                                    \
    typedef struct name {                                                                          \
        uint32_t id;                                                                               \
    } name NK_HANDLE

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

/* Typed resource identifiers. These aliases keep the same four-byte ABI as
 * nk_handle while allowing language bindings to preserve each kind nominally.
 * Use nk_handle only where an API intentionally accepts more than one kind. */
typedef uint32_t nk_window NK_HANDLE NK_HANDLE_DESTROY(nk_window_destroy);
typedef uint32_t nk_surface NK_HANDLE NK_HANDLE_DESTROY(nk_surface_destroy);
typedef uint32_t nk_webview NK_HANDLE NK_HANDLE_DESTROY(nk_webview_destroy);
typedef uint32_t nk_monitor NK_HANDLE;
typedef uint32_t nk_cursor NK_HANDLE NK_HANDLE_DESTROY(nk_cursor_destroy);
typedef uint32_t nk_joystick NK_HANDLE;
typedef uint32_t nk_mobile_host NK_HANDLE NK_HANDLE_DESTROY(nk_mobile_host_destroy);
typedef uint32_t nk_resource_stream NK_HANDLE NK_HANDLE_DESTROY(nk_resource_close);
/** Generation-checked handle for a file-system watcher. */
typedef uint32_t nk_file_watch NK_HANDLE NK_HANDLE_DESTROY(nk_file_watch_destroy);
/** Generation-checked handle for clipboard change observation. */
typedef uint32_t nk_clipboard_watch NK_HANDLE NK_HANDLE_DESTROY(nk_clipboard_watch_stop);

/** Opaque generation-checked identifier for a live NativeKit resource. */
typedef uint32_t nk_handle NK_HANDLE;

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
typedef uint32_t nk_bool NK_BOOL32;

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

/*
 * All failures have a thread-local diagnostic available from nk_last_error().
 * INVALID_ARGUMENT describes caller data; INVALID_HANDLE describes a stale or
 * wrong-kind resource; INVALID_REQUEST describes a valid operation that can no
 * longer complete in its current lifecycle state.
 */
enum NK_ENUM(nk_result) {
    /** Operation completed successfully. */
    NK_OK = 0,
    /** Operation was accepted and will complete through a later event. */
    NK_PENDING = 1,
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
    NK_ERROR_BUFFER_TOO_SMALL = -11,
    /** A requested service, method, plugin, or named object does not exist. */
    NK_ERROR_NOT_FOUND = -12,
    /** A payload exceeds the bounded size accepted by the target contract. */
    NK_ERROR_PAYLOAD_TOO_LARGE = -13,
    /** A task or asynchronous operation was cancelled before completion. */
    NK_ERROR_CANCELLED = -14
};

/* ------------------------------------------------------------------------- */
/* Event kinds                                                               */
/* ------------------------------------------------------------------------- */

enum NK_ENUM(nk_event_kind) {
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
    /** The physical device orientation changed. */
    NK_EVENT_DEVICE_ORIENTATION_CHANGED = 22,
    /** The application display orientation changed. */
    NK_EVENT_DISPLAY_ORIENTATION_CHANGED = 23,
    /** A Web physical-device-orientation permission request completed. */
    NK_EVENT_DEVICE_ORIENTATION_PERMISSION_COMPLETE = 24,
    /** A raw sensor sample was produced; one pending sample is coalesced per sensor. */
    NK_EVENT_SENSOR_UPDATE = 25,
    /** An asynchronous raw-sensor permission request completed. */
    NK_EVENT_SENSOR_PERMISSION_COMPLETE = 26,
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
    /** Clipboard contents may have changed; read metadata through the clipboard API. */
    NK_EVENT_CLIPBOARD_CHANGED = 403,
    /** A watched file or directory was added, removed, modified, or moved. */
    NK_EVENT_FILE_CHANGED = 410,
    /** A file-watch queue overflow requires a complete directory rescan. */
    NK_EVENT_FILE_WATCH_OVERFLOW = 411,
    /** A notification was delivered by the platform. */
    NK_EVENT_NOTIFICATION_DELIVERED = 500,
    /** A user activated a notification. */
    NK_EVENT_NOTIFICATION_ACTIVATED = 501,
    /** A notification was dismissed. */
    NK_EVENT_NOTIFICATION_DISMISSED = 502,
    /** A notification could not be shown or completed. */
    NK_EVENT_NOTIFICATION_FAILED = 503,
    /** The user activated an application menu item. */
    NK_EVENT_MENU_ITEM_ACTIVATED = 510,
    /** The user requested that the application terminate. */
    NK_EVENT_APPLICATION_QUIT_REQUESTED = 511,
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
    NK_EVENT_RESOURCE_DROP = 802,
    /** An asynchronous URI resource load completed with raw byte data. */
    NK_EVENT_RESOURCE_DATA_COMPLETE = 803,
    /** HTTP response headers became available. */
    NK_EVENT_HTTP_HEADERS = 900,
    /** A streaming HTTP response has data available to read. */
    NK_EVENT_HTTP_DATA_AVAILABLE = 901,
    /** HTTP transfer progress was coalesced and is available to inspect. */
    NK_EVENT_HTTP_PROGRESS = 902,
    /** An HTTP request reached its terminal state. */
    NK_EVENT_HTTP_COMPLETE = 903,
    /** A non-looping audio voice reached its natural end; source is its voice handle. */
    NK_EVENT_AUDIO_VOICE_COMPLETE = 904,
    /** A plugin service call reached its terminal state. */
    NK_EVENT_PLUGIN_COMPLETE = 1000,
    /** A plugin emitted an unsolicited notification. */
    NK_EVENT_PLUGIN_EVENT = 1001,
    /** A native task reported copied progress data. */
    NK_EVENT_TASK_PROGRESS = 1100,
    /** A native task completed successfully with copied result data. */
    NK_EVENT_TASK_COMPLETE = 1101,
    /** A native task failed with an optional copied diagnostic/result payload. */
    NK_EVENT_TASK_FAILED = 1102,
    /** A native task was cancelled with an optional copied result payload. */
    NK_EVENT_TASK_CANCELLED = 1103
};

/* ------------------------------------------------------------------------- */
/* Logical executors                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Logical executor that owns an API contract or callback.
 *
 * Executors name thread-affinity domains rather than threads. One NativeKit
 * runtime currently binds NK_EXECUTOR_PLATFORM, NK_EXECUTOR_APP, and
 * NK_EXECUTOR_RENDER to the thread that called nk_init(), because rendering and
 * event delivery still share that thread. The distinction is part of the
 * contract now so that application or render work can move to another thread
 * later without redesigning the APIs that declared affinity.
 */
typedef uint32_t nk_executor;

enum NK_ENUM(nk_executor) {
    /** Platform objects: windows, surfaces, monitors, and presentation. */
    NK_EXECUTOR_PLATFORM = 0,
    /** Application callbacks and event delivery; the nk_init() thread. */
    NK_EXECUTOR_APP = 1,
    /** Rendering against an immutable frame target. */
    NK_EXECUTOR_RENDER = 2,
    /** CPU work on arbitrary native threads; no thread affinity is implied. */
    NK_EXECUTOR_WORKER = 3
};

/**
 * Task queued by nk_dispatch_to_app(). The function runs on the application
 * executor and must not propagate exceptions across the C ABI.
 */
typedef void(NK_CALL *nk_task_fn)(void *NK_NULLABLE user_data);

/* ------------------------------------------------------------------------- */
/* Initialization and event data                                             */
/* ------------------------------------------------------------------------- */

/** Options used to start one NativeKit runtime generation. */
typedef struct nk_init_options {
    /** Size of this structure in bytes; old prefixes remain valid. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** ABI version requested by the caller; normally NK_API_VERSION. */
    uint32_t api_version;
    /** Event queue capacity; zero selects the backend default. */
    uint32_t event_queue_capacity;
    /** Reserved for compatible extensions; initialize to zero. */
    uint32_t reserved;
    /** Stable reverse-DNS or package-style application identifier. */
    const char *application_id NK_NULLABLE_UTF8;
    /** Optional human-readable application name. */
    const char *application_name NK_NULLABLE_UTF8;
} nk_init_options;

/**
 * Variable-length event data is owned by NativeKit. It remains valid until
 * nk_event_release() is called. A successfully polled event must be released,
 * even when data is NULL. Strings are UTF-8 and data_size excludes any trailing
 * NUL byte. Callers must zero-initialize this structure and set struct_size.
 */
typedef struct nk_event {
    /** Size of this structure in bytes; must be set before polling. */
    uint32_t struct_size NK_STRUCT_SIZE;
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
 * Returns the generation identifier of the active runtime, or zero when no
 * runtime is active. Every succeeded nk_init() produces a new, non-zero
 * generation. Long-lived native callbacks and plugin instances capture the
 * generation they were created in and treat a different value as invalid.
 */
NK_API uint64_t NK_CALL nk_runtime_generation(void);

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

/* ------------------------------------------------------------------------- */
/* Application dispatch                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Returns the canonical executor of the calling thread. While the platform,
 * application, and render executors are aliased to one thread, that thread
 * reports NK_EXECUTOR_APP. Threads that are not bound to a runtime, including
 * threads used before nk_init() or after nk_shutdown(), report
 * NK_EXECUTOR_WORKER.
 */
NK_API nk_executor NK_CALL nk_executor_current(void);

/**
 * Reports whether the calling thread satisfies the affinity of `executor`.
 * While the platform, application, and render executors are aliased, the
 * nk_init() thread satisfies all three. NK_EXECUTOR_WORKER is satisfied by any
 * other thread while a runtime is active, because it never implies affinity.
 */
NK_API nk_bool NK_CALL nk_executor_is_current(nk_executor executor);

/**
 * Queues native work onto the application executor.
 *
 * The call never invokes `fn` directly, so native callbacks that run on
 * arbitrary threads have a sanctioned way back onto the thread that polls
 * events. Queued tasks run in a detached batch on the next nk_poll_event()
 * call, before that call returns an event; work queued from inside a task runs
 * on the following poll. The `user_data` pointer is passed through unchanged
 * and is not owned by NativeKit.
 *
 * Tasks still queued when nk_shutdown() completes are discarded without being
 * invoked. Internal runtime-owned tasks also carry a byte cost and a noexcept
 * cleanup callback, so queued plugin work is reclaimed when it is discarded;
 * the public user_data pointer remains caller-owned.
 *
 * @param fn Non-null task function.
 * @param user_data Optional opaque value passed to `fn`.
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for a null function,
 *         NK_ERROR_NOT_INITIALIZED without an active runtime, or an error when
 *         the bounded dispatch queue cannot accept the task.
 */
NK_API nk_result NK_CALL nk_dispatch_to_app(nk_task_fn fn, void *NK_NULLABLE user_data);

#ifdef __cplusplus
}
#endif

#endif
