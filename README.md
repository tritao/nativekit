# NativeKit

NativeKit is an experimental cross-platform native desktop-services library
with a small, stable C ABI. It is intended to provide windows, dialogs, shell
integration, clipboard and drag/drop, WebViews, and small system queries without
exposing a C++ application framework to consumers.

wxWidgets is vendored as a Git submodule at `vendor/wxWidgets`, tracking its
upstream `master` branch. The parent repository always pins an exact commit;
`tools/upstream-lock.json` records the same revision for provenance reporting.

Clone with the nested wxWidgets dependencies initialized:

```sh
git clone --recurse-submodules <nativekit-url>
```

For an existing checkout, use `git submodule update --init --recursive`.

Windows smoke tests can be cross-built and run in an isolated Wine prefix with
`tools/test-wine.sh`; see `docs/testing.md` for prerequisites and limitations.
The Windows backend currently provides Win32-owned windows, asynchronous COM
file/save/directory and native message dialogs, shell integration, standard
directories, locale, desktop appearance, clipboard text/files, and file drops.
It also provides WebView2 when the Evergreen runtime is installed. CMake fetches
the pinned Microsoft WebView2 SDK by default; use `-DNK_ENABLE_WEBVIEW2=OFF` for
an offline Windows build without WebView support. Windows text drops are deferred
until the backend has an OLE drop target.

WebView creation may be asynchronous. Consumers can wait for
`NK_EVENT_WEBVIEW_READY`; navigation, HTML, and evaluation calls made before that
event are retained in call order.

The current Linux backend provides NativeKit-owned GTK 3 windows, WebKitGTK
WebViews, asynchronous native dialogs, shell launching, standard directories,
locale, desktop appearance, clipboard, and file/text drops. Builds without GTK 3 and WebKitGTK 4.1 retain
the same ABI and report these capabilities as unsupported.

The initial macOS backend provides Cocoa-owned windows, asynchronous native file
and message panels, workspace shell integration, standard directories, locale,
appearance, pasteboard text and file transfer, file/text drops, and native
NSWindow/NSView descriptors. Its WKWebView child backend supports navigation,
HTML content, JavaScript evaluation, page messages, and lifecycle events.

## Build

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the minimal browser example with:

```sh
./build/examples/nativekit_browser https://example.com
```

UI APIs will be main-thread-only. Event payloads returned by `nk_poll_event`
must be released using `nk_event_release`.

## Status

The ABI is pre-1.0 and not yet stable. wxWidgets provenance will be recorded in
`tools/upstream-lock.json` before any upstream-derived implementation is added.
