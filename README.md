# NativeKit

NativeKit is an experimental cross-platform native desktop-services library
with a small, stable C ABI. It is intended to provide windows, dialogs, shell
integration, clipboard and drag/drop, WebViews, and small system queries without
exposing a C++ application framework to consumers.

Multiple top-level windows may form a small ownership tree for utility,
borderless, and modal windows. This is a lifetime and native-stacking model, not
a portable widget hierarchy; NativeKit does not introduce frames or panels.

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
It also provides notification-area messages and WebView2 when the Evergreen
runtime is installed. CMake fetches
the pinned Microsoft WebView2 SDK by default; use `-DNK_ENABLE_WEBVIEW2=OFF` for
an offline Windows build without WebView support. Windows text drops are deferred
until the backend has an OLE drop target.

WebView creation may be asynchronous. Consumers can wait for
`NK_EVENT_WEBVIEW_READY`; navigation, HTML, and evaluation calls made before that
event are retained in call order.

Navigation policy is opt-in with `NK_WEBVIEW_NAVIGATION_POLICY`. Proposed
navigations become request-ID events that callers explicitly allow or
cancel with `nk_webview_navigation_decide()`.

JavaScript evaluation results and page-to-native messages are compact UTF-8
JSON on every backend, preserving value types across language boundaries.

The current Linux backend provides NativeKit-owned GTK 3 windows, WebKitGTK
WebViews, asynchronous native dialogs, shell launching, standard directories,
locale, desktop appearance, clipboard, file/text drops, and freedesktop
notifications. Builds without GTK 3 and WebKitGTK 4.1 retain
the same ABI and report these capabilities as unsupported.

The initial macOS backend provides Cocoa-owned windows, asynchronous native file
and message panels, workspace shell integration, standard directories, locale,
appearance, pasteboard text and file transfer, file/text drops, and native
NSWindow/NSView descriptors. It uses the macOS user-notification service with
explicit permission failures. Its WKWebView child backend supports navigation,
HTML content, JavaScript evaluation, page messages, and lifecycle events.

The experimental Android backend attaches to a caller-owned `ViewGroup` rather
than creating an Activity. It provides child WebViews, JSON page messages,
JavaScript evaluation, navigation policy, and explicit lifecycle forwarding.
The Gradle library and host sample live under `android/`; an Android SDK and NDK
are required to build them.

## Build

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Start with the embedded-page example. It demonstrates the complete lifecycle,
responsive WebView bounds, and a JSON message from JavaScript to C:

```sh
./build/examples/nativekit_hello
```

The browser example adds capability detection, navigation policy, page titles,
failure reporting, and an optional starting URL:

```sh
./build/examples/nativekit_browser https://example.com
```

For an interactive tour of the desktop API, run NativeKit Lab. Its HTML control
panel exercises native dialogs, clipboard and drops, notifications, system
queries, window ownership and state, shell integration, JavaScript evaluation,
and a second browser window while showing asynchronous results in a live log:

```sh
./build/examples/nativekit_showcase
```

Source formatting is defined by `.clang-format`. When ClangFormat is installed,
CMake provides targets to apply it or verify that no changes are needed:

```sh
cmake --build build --target format
cmake --build build --target format-check
```

UI APIs will be main-thread-only. Event payloads returned by `nk_poll_event`
must be released using `nk_event_release`.

## Status

The ABI is pre-1.0 and not yet stable. wxWidgets provenance will be recorded in
`tools/upstream-lock.json` before any upstream-derived implementation is added.
