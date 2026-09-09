# NativeKit

NativeKit is an experimental cross-platform native desktop-services library
with a small, stable C ABI. It is intended to provide windows, dialogs, shell
integration, clipboard and drag/drop, WebViews, and small system queries without
exposing a C++ application framework to consumers.

The current milestone contains the ABI foundation: initialization, errors,
generation-checked internal handles, and an owned event queue. Platform window
and WebView backends are the next milestone.

## Build

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

UI APIs will be main-thread-only. Event payloads returned by `nk_poll_event`
must be released using `nk_event_release`.

## Status

The ABI is pre-1.0 and not yet stable. wxWidgets provenance will be recorded in
`tools/upstream-lock.json` before any upstream-derived implementation is added.

