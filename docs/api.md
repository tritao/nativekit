# NativeKit API contracts

NativeKit's public headers are the normative API reference. This document
collects contracts that span more than one function.

## Threading

The thread that successfully calls `nk_init()` becomes the UI thread. Window,
WebView, dialog, and event APIs must be called from that thread. A call made on
another thread returns `NK_ERROR_WRONG_THREAD`. `nk_last_error()` is thread-local.

## Handles

Handles identify resources without exposing native or C++ pointers. They contain
a slot and generation, so a handle becomes invalid as soon as its resource is
destroyed and cannot accidentally identify a later occupant of the same slot.
Destroying a window also invalidates all child WebView handles.

## Native-window interoperability

`nk_window_get_native()` is the only intentional escape from opaque handles. It
returns a borrowed descriptor containing pointer-sized platform values. NativeKit
retains ownership, and all values become invalid when the window is destroyed.
On GTK the descriptor identifies either an X11 display/window pair or a Wayland
display/surface pair.

Wrapping caller-owned windows is a separate capability because detaching safely
requires backend-specific event and widget ownership. The GTK backend currently
returns `NK_ERROR_UNSUPPORTED` and does not advertise `NK_CAP_WRAP_NATIVE_WINDOW`.

## Mobile hosts

Mobile applications attach a caller-owned native container with
`nk_mobile_host_attach()` instead of creating a desktop top-level window. The
returned host handle may be passed as the parent to `nk_webview_create()`.
Destroying it first destroys its NativeKit-owned WebViews and then releases the
backend reference; it never destroys the Activity, view controller, or native
container.

The host application forwards active, inactive, and background transitions with
`nk_mobile_host_set_lifecycle()`. On Android, attach must run on the main thread
and receives the current `JNIEnv*` and a `ViewGroup`. Those JNI values are used
only during the call, and the backend retains its own global reference.

## Window ownership

NativeKit supports multiple independent top-level windows. A new window may
optionally name an existing window as its owner. Utility, borderless, and modal
windows remain top-level native windows; they are not generic child widgets.

Ownership controls native stacking and modality and transfers lifetime:
destroying an owner recursively destroys its owned windows and invalidates their
handles. A modal window must have an owner. Because owners must already exist at
creation time, the ownership graph cannot contain cycles. WebViews remain the
only portable native child surface in the initial API.

Window state is queried as a versioned value and changes are emitted as
`NK_EVENT_WINDOW_STATE_CHANGED`. Minimize, maximize, restore, activation,
fullscreen, attention, and logical-pixel size constraints are explicit
operations; they do not introduce a command or widget hierarchy.

## Events and payloads

`nk_poll_event()` returns events in FIFO order. An empty queue is not an error: it
returns `NK_OK` with kind `NK_EVENT_NONE`. Every successfully returned event must
be passed to `nk_event_release()`. The release preserves `struct_size`, allowing
the same structure to be polled again.

Text event payloads use UTF-8 bytes in `data`; `data_size` excludes a trailing NUL
and consumers must not assume one exists. Current payloads are:

| Event | Source | Request | Data |
|---|---|---|---|
| `NK_EVENT_WINDOW_CLOSE` | window | none | empty |
| `NK_EVENT_WINDOW_RESIZE` | window | none | `nk_window_resize_event` |
| `NK_EVENT_WINDOW_SCALE_CHANGED` | window | none | `nk_window_scale_event` |
| `NK_EVENT_WINDOW_STATE_CHANGED` | window | none | `nk_window_state` |
| `NK_EVENT_WEBVIEW_NAVIGATED` | WebView | none | resulting URL |
| `NK_EVENT_WEBVIEW_TITLE_CHANGED` | WebView | none | page title |
| `NK_EVENT_WEBVIEW_MESSAGE` | WebView | none | JSON value or serialization error text |
| `NK_EVENT_WEBVIEW_EVAL_COMPLETE` | WebView | evaluation ID | JSON result or error text |
| `NK_EVENT_WEBVIEW_NAVIGATION_FAILED` | WebView | none | error text; category in `flags` |
| `NK_EVENT_WEBVIEW_PROCESS_TERMINATED` | WebView | none | empty; backend reason in `flags` |
| `NK_EVENT_WEBVIEW_NAVIGATION_REQUEST` | WebView | navigation ID | proposed URL |
| `NK_EVENT_NOTIFICATION_DELIVERED` | none | notification ID | empty |
| `NK_EVENT_NOTIFICATION_ACTIVATED` | none | notification ID | optional platform action identifier |
| `NK_EVENT_NOTIFICATION_DISMISSED` | none | notification ID | empty; platform reason may be in `flags` |
| `NK_EVENT_NOTIFICATION_FAILED` | none | notification ID | diagnostic text |

A close event is a request: the window remains alive until the application calls
`nk_window_destroy()`.

A WebView created with `NK_WEBVIEW_NAVIGATION_POLICY` pauses each navigation
proposal exposed by the native backend and emits
`NK_EVENT_WEBVIEW_NAVIGATION_REQUEST`. Resolve it once with
`nk_webview_navigation_decide()`. Destroying the WebView cancels its
pending requests. When the event queue is full, navigation is allowed so the
native engine cannot be left indefinitely suspended. WebView2 has no navigation
deferral API, so its Windows backend cancels and replays an allowed URL; replayed
form submissions therefore become ordinary URL navigations.

WebView evaluation results and page messages use compact UTF-8 JSON on every
backend. JSON strings retain their quotes. Numbers, booleans, arrays, objects,
and `null` preserve their JSON types. Values that JSON cannot represent complete
with a failing event result when the native engine exposes the serialization
failure.

Every successfully started WebView evaluation has exactly one terminal event.
Destroying its WebView, directly or through parent-window destruction, completes
the request with `NK_ERROR_INVALID_REQUEST`. Native callbacks arriving after
that cancellation are ignored. Terminal request events may temporarily exceed
the configured event-queue capacity so they cannot be lost behind ordinary
notifications.

Each successful `nk_init()` starts a distinct internal runtime generation.
Asynchronous platform contexts capture that generation, and callbacks from an
earlier generation are discarded after shutdown or reinitialization. Runtime
generations are internal and do not alter the public handle or request-ID ABI.

Consecutive pending resize events for the same window are coalesced. Events of
other kinds preserve their position relative to resize events.

## Capability queries

`nk_get_capabilities()` describes the compiled backend. Callers must still handle
runtime failures—for example, a Linux build can include GTK support but be unable
to connect to a display.

## Shell and system strings

Shell operations submit work to the desktop on the UI thread. Reveal first uses
the freedesktop file-manager interface and falls back to opening the containing
directory when that interface is unavailable.

Standard directories and locale queries are initialization-independent and may
be called from any thread. They use a two-call buffer convention: query the size
including NUL, allocate, then call again. A short buffer is never partially filled.

## Clipboard and drops

Clipboard writes copy their input synchronously. Reads remain asynchronous because
the selection owner may be another process, particularly under Wayland. File-list
results use `nk_clipboard_files` followed by NUL-terminated UTF-8 paths and are
decoded with `nk_clipboard_event_file()`.

Windows opt into drops explicitly. Drop event data starts with `nk_drop_data`,
including logical window coordinates, followed by strings decoded through
`nk_drop_event_item()`. Only local file URIs are emitted as file drops.

## Dialog results

Dialogs never run a nested blocking loop. Starting one returns a request ID and
completion arrives through `NK_EVENT_DIALOG_COMPLETE`. File and directory results
begin with `nk_dialog_paths`, followed by a table of 32-bit offsets and NUL-terminated
UTF-8 paths. Consumers should use `nk_dialog_event_path()` instead of parsing this
layout directly. Cancellation is a successful completion with `accepted == 0`.

## Notifications

Notification submission is asynchronous. `nk_notification_show()` copies its
inputs and returns a request ID; `DELIVERED` or `FAILED` reports whether the
desktop accepted it. Activation and dismissal may arrive later. Explicitly
closing a live request removes the native notification and emits `DISMISSED`.

Notification availability is still subject to runtime policy. The Linux desktop
must provide `org.freedesktop.Notifications`, macOS may request user permission,
and Windows must expose a notification area. These failures remain observable
instead of being treated as successful delivery.
