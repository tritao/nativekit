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

The current Linux backend provides NativeKit-owned GTK 3 windows, WebKitGTK
WebViews, asynchronous native dialogs, shell launching, standard directories,
locale, and desktop appearance. Builds without GTK 3 and WebKitGTK 4.1 retain
the same ABI and report these capabilities as unsupported.

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
