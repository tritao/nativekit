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
| `NK_EVENT_WEBVIEW_NAVIGATED` | WebView | none | resulting URL |
| `NK_EVENT_WEBVIEW_TITLE_CHANGED` | WebView | none | page title |
| `NK_EVENT_WEBVIEW_MESSAGE` | WebView | none | JavaScript value converted to text |
| `NK_EVENT_WEBVIEW_EVAL_COMPLETE` | WebView | evaluation ID | result or error text |
| `NK_EVENT_WEBVIEW_NAVIGATION_FAILED` | WebView | none | error text; category in `flags` |
| `NK_EVENT_WEBVIEW_PROCESS_TERMINATED` | WebView | none | empty; backend reason in `flags` |

A close event is a request: the window remains alive until the application calls
`nk_window_destroy()`.

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

## Dialog results

Dialogs never run a nested blocking loop. Starting one returns a request ID and
completion arrives through `NK_EVENT_DIALOG_COMPLETE`. File and directory results
begin with `nk_dialog_paths`, followed by a table of 32-bit offsets and NUL-terminated
UTF-8 paths. Consumers should use `nk_dialog_event_path()` instead of parsing this
layout directly. Cancellation is a successful completion with `accepted == 0`.
