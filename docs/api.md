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

Android hosts also forward Activity intents with
`nk_mobile_host_dispatch_event()`. The Java `NativeKitHost.dispatchIntent()`
wrapper supplies the JNI details: call it for the Activity's initial
`getIntent()` after attaching the host and again from `onNewIntent()`. Recognized
`ACTION_VIEW`, `ACTION_SEND`, and `ACTION_SEND_MULTIPLE` intents become queued
resource or sharing events. Unrecognized actions return `NK_ERROR_UNSUPPORTED`.

Container size, display scale, system-bar safe insets, and software-keyboard
inset changes produce `NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED`. Geometry and
WebView bounds use logical pixels on mobile just as they do on desktop.

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
| `NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED` | mobile host | none | `nk_mobile_host_geometry` |
| `NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE` | none | clipboard read ID | `nk_resource_list` |
| `NK_EVENT_RESOURCE_OPENED` | mobile host | none | `nk_resource_list` |
| `NK_EVENT_SHARE_RECEIVED` | mobile host | none | `nk_received_share` followed by its resource list and strings |
| `NK_EVENT_KEY` | window | none | `nk_key_event` |
| `NK_EVENT_TEXT_INPUT` | window | none | `nk_text_input_event` |
| `NK_EVENT_POINTER_MOVE` | window | none | `nk_pointer_move_event` |
| `NK_EVENT_POINTER_BUTTON` | window | none | `nk_pointer_button_event` |
| `NK_EVENT_POINTER_SCROLL` | window | none | `nk_pointer_scroll_event` |
| `NK_EVENT_POINTER_ENTER` | window | none | empty; `flags` is one on enter and zero on leave |
| `NK_EVENT_WINDOW_MOVE` | window | none | `nk_window_move_event` |
| `NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE` | window | none | `nk_window_framebuffer_resize_event` |
| `NK_EVENT_MONITOR_CONNECTED` | monitor | none | empty |
| `NK_EVENT_MONITOR_DISCONNECTED` | invalidated monitor | none | empty |
| `NK_EVENT_JOYSTICK_CONNECTED` | joystick | none | empty |
| `NK_EVENT_JOYSTICK_DISCONNECTED` | invalidated joystick | none | empty |
| `NK_EVENT_JOYSTICK_AXIS` | joystick | none | `nk_joystick_axis_event` |
| `NK_EVENT_JOYSTICK_BUTTON` | joystick | none | `nk_joystick_button_event` |
| `NK_EVENT_JOYSTICK_HAT` | joystick | none | `nk_joystick_hat_event` |
| `NK_EVENT_GAMEPAD_AXIS` | mapped joystick | none | `nk_gamepad_axis_event` |
| `NK_EVENT_GAMEPAD_BUTTON` | mapped joystick | none | `nk_gamepad_button_event` |
| `NK_EVENT_SURFACE_READY` | graphics surface | none | empty |
| `NK_EVENT_SURFACE_RESIZE` | graphics surface | none | `nk_surface_resize_event` |

A close event is a request: the window remains alive until the application calls
`nk_window_destroy()`.

`NK_EVENT_WEBVIEW_PROCESS_TERMINATED` is terminal for its source WebView. The
backend invalidates that handle before publishing the event, cancels outstanding
operations, and leaves the parent alive so the application can create a replacement.

Keyboard events report a normalized key and the platform scancode separately.
Text input is delivered as Unicode code points and is distinct from physical key
transitions. Pointer coordinates are logical pixels relative to the window
content. Consecutive pointer-move events may be coalesced; key and button
transitions are never coalesced.

The GTK backend routes key events through a per-window input-method context, so
dead-key composition, active keyboard layouts, and IME committed text are
reported through `NK_EVENT_TEXT_INPUT`.

Cursor resources may be standard platform shapes or copied RGBA8 images.
Destroying a cursor handle does not invalidate a cursor already selected by a
window. GTK supports normal, hidden, and captured pointer modes. Disabled
relative-pointer mode and raw motion are reported as unsupported because GTK 3
cannot provide consistent behavior across X11 and Wayland.

Window content sizes use logical pixels while framebuffer sizes use device
pixels. Content scale is available per axis. Move, logical resize, and
framebuffer resize events are coalesced independently. Global position and frame
extents are unavailable on Wayland and return `NK_ERROR_UNSUPPORTED`.

GTK windows support runtime resizability, decorations, keep-above behavior,
opacity, pointer passthrough, aspect-ratio constraints, and hover queries.

## Monitors and fullscreen

Monitor handles remain stable while their GDK monitor is connected. A
disconnect event carries the handle that has just been invalidated. Geometry
and work areas use logical screen coordinates; current video-mode dimensions
use device pixels and refresh rates use hertz. GTK 3 exposes only the current
compositor mode, so mode enumeration currently returns one entry and exclusive
mode switching is not supported.

`nk_window_set_fullscreen_monitor()` requests borderless fullscreen on a
specific monitor. Passing `NK_INVALID_HANDLE` leaves fullscreen and lets the
window manager restore the previous windowed placement.

## Linux joysticks

Include `nativekit_joystick.h` for raw joystick access. The GTK/Linux backend
discovers evdev devices under `/dev/input`, tracks hotplug with inotify, and
exposes generation-checked handles through `nk_joystick_list()`. Connection and
removal produce `NK_EVENT_JOYSTICK_CONNECTED` and
`NK_EVENT_JOYSTICK_DISCONNECTED`.

The axes, buttons, and hats calls return current raw state. Axes are normalized
to `[-1, 1]`, and hats use the `NK_JOYSTICK_HAT_*` bit flags. Name and
SDL-compatible GUID queries use the usual two-call buffer-size pattern.
Controller mappings and standardized gamepad names are intentionally separate
from this transport layer.

If the evdev reader falls behind, Linux reports `SYN_DROPPED`; NativeKit ignores
the incomplete packet and resynchronizes all axes, buttons, and hats from kernel
state at the next report boundary. `nk_joystick_get_diagnostics()` exposes the
first transport warning observed since initialization, including missing
`/dev/input`, device permission failures, and unavailable hotplug monitoring.

## Standard gamepads

Include `nativekit_gamepad.h` to translate a raw joystick into the conventional
A/B/X/Y, shoulder, stick, trigger, and D-pad layout. NativeKit matches
SDL-compatible joystick GUIDs against SDL/GLFW controller mapping strings.
`nk_gamepad_add_mapping()` installs or replaces a mapping for the current
process; application mappings take precedence over the small built-in set.

Use `nk_gamepad_is_mapped()` before requesting the mapped name or state. The
mapping parser supports button, axis, half-axis, inverted-axis, and hat inputs.
Unmapped devices return `NK_ERROR_UNSUPPORTED` from name and state queries.

`nk_gamepad_add_mappings()` ingests a newline-separated SDL database atomically,
skipping comments, blank lines, and mappings for other platforms. Later entries
for the same GUID take precedence. `nk_gamepad_get_mapping_source()` reports
whether the active entry came from NativeKit's built-in database or an
application update.

NativeKit ships a compact Linux subset of the community-maintained
SDL_GameControllerDB. The complete pinned source and zlib license are retained
under `vendor/SDL_GameControllerDB`; `scripts/update_gamepad_db.py` refreshes the
snapshot and generated table reproducibly. Applications can query the exact
40-character upstream revision with
`nk_gamepad_get_builtin_database_revision()`.

Raw input changes produce `NK_EVENT_JOYSTICK_AXIS`, `BUTTON`, and `HAT` events.
Mapped devices additionally produce canonical `NK_EVENT_GAMEPAD_AXIS` and
`BUTTON` events after each complete evdev report. Axis events for the same
device and axis coalesce when adjacent; button and hat ordering is preserved.
Input delivery is independent of GTK window focus.

`nk_gamepad_set_options()` configures radial stick dead zones, trigger dead
zones, and optional `[0, 1]` trigger output. The same normalization applies to
state queries and generated gamepad events.

## Graphics surfaces

Graphics surfaces are separate resources attached to NativeKit-owned windows.
The GTK backend implements OpenGL and OpenGL ES surfaces with `GtkGLArea`.
Applications call `nk_surface_make_current()`, render, and then call
`nk_surface_present()`. GTK owns the final framebuffer composition, so
presentation schedules a `GtkGLArea` render instead of directly swapping a
caller-owned native surface. Contexts can reuse another surface's GTK context;
the shared source must outlive its dependents. Procedure lookup resolves both
core and extension entry points for the current GL implementation.

## Vulkan surfaces

Include `nativekit_vulkan.h` for Vulkan presentation. NativeKit loads
`libvulkan.so.1` at runtime, so neither the Vulkan SDK nor loader is a build-time
dependency. Given a window, the extension query returns `VK_KHR_surface` plus
the active GTK display extension: `VK_KHR_xlib_surface` or
`VK_KHR_wayland_surface`.

`nk_vulkan_create_surface()` accepts a `VkInstance` cast to `void *` and returns
the bits of a `VkSurfaceKHR`. The application owns that surface and must call
`nk_vulkan_destroy_surface()` before destroying its Vulkan instance. Both calls
accept an optional `VkAllocationCallbacks` pointer and require the relevant
instance extensions to have been enabled.

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

On Android, shell URL and clipboard operations use the `Context` of an attached
mobile host. Text clipboard reads retain the cross-platform asynchronous event
contract even though Android provides the value synchronously.

Android directory queries return app-scoped storage: home, config, and data map
to the app files directory; cache and temporary storage map to the cache
directory; documents and downloads use their app-specific external directories.
Desktop is unsupported. Locale and appearance follow the attached host's current
configuration, including per-app locale and day/night changes.

Incoming Android resources retain their `content://` URI identity. Decode
individual resources in either incoming event with `nk_resource_event_item()`;
do not treat the URI as a filesystem path. A share may contain zero or more
resources plus optional UTF-8 text and subject, available through
`nk_share_event_text()` and `nk_share_event_subject()`. Missing optional strings
are returned as empty strings. URI grant flags describe the access conveyed by
the Intent, and streams are opened through `nk_resource_open_stream()`.

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

## Time and event waits

`nativekit_time.h` exposes monotonic nanosecond and floating-point second clocks;
their unspecified epoch is stable for the process and does not depend on
`nk_init()`. Use differences between readings rather than interpreting them as
wall-clock timestamps.

`nk_wait_events()` blocks the UI thread until NativeKit queues an event or
another thread calls `nk_wake_events()`. The timeout variant uses a monotonic
deadline and returns `NK_OK` on either an event, explicit wake, or timeout; call
`nk_poll_event()` afterwards to distinguish an event from an empty wake. GTK is
pumped in bounded blocking slices, and already-queued events return immediately.

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

Android uses the Storage Access Framework for open, save, and directory dialogs.
Its path-based dialog entry points are unsupported because document-provider
results are not filesystem paths. Use the resource dialog variants, which return
the original `content:` URI and access flags through `nk_resource_event_item()`.

## URI resources and sharing

`nk_resource` carries an absolute URI plus optional MIME type and display name.
It deliberately does not expose a filesystem path. Resource-bearing dialog and
clipboard events use `nk_resource_list`; decode individual items with
`nk_resource_event_item()` while the event remains alive.

`nk_shell_open_resource()` views one URI. `nk_share()` hands optional text and
resource URIs to the platform share UI; success means the share UI was launched,
not that a recipient consumed the content. Resource clipboard reads remain
asynchronous and complete with `NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE`.

Android forwards `content:` grants through intents and never exports `file:` URIs.
Callers must keep the URI and respect its reported readable, writable, and
persisted flags rather than attempting to derive a local path.

URI contents are accessed through opaque resource-stream handles.
`nk_resource_open()` runs on the UI thread and accepts explicit read, write,
create, and truncate flags. Reads, writes, seeks, information queries, and close
may run on worker threads. Reads and writes may transfer fewer bytes than
requested; callers repeat them until complete. A stream advertises whether it is
seekable and whether its size is known. Unknown sizes are reported as
`UINT64_MAX`.

Android opens both `content:` and local `file:` resources through
`ContentResolver`, retaining the detached descriptor until the stream closes.
Desktop backends open RFC 8089 local `file:` URIs directly and reject other
schemes. `NK_CAP_RESOURCE_IO` reports this support independently from system
sharing support.

## Notifications

Notification submission is asynchronous. `nk_notification_show()` copies its
inputs and returns a request ID; `DELIVERED` or `FAILED` reports whether the
desktop accepted it. Activation and dismissal may arrive later. Explicitly
closing a live request removes the native notification and emits `DISMISSED`.

Notification availability is still subject to runtime policy. The Linux desktop
must provide `org.freedesktop.Notifications`, macOS may request user permission,
and Windows must expose a notification area. These failures remain observable
instead of being treated as successful delivery.

Android creates a default notification channel and requests notification
permission through an internal proxy activity when required. Taps and user
dismissals are routed through an internal receiver while the NativeKit runtime
is alive; permission denial produces `NK_EVENT_NOTIFICATION_FAILED`.
