# NativeKit 🧰

**Native desktop capabilities behind one compact C ABI.**

NativeKit gives runtimes, language bindings, and lightweight applications access
to windows, WebViews, dialogs, graphics surfaces, input, system services, and
mobile hosts without exposing a C++ application framework. The API is handle
based, UTF-8 throughout, and designed around an explicit event queue.

> [!IMPORTANT]
> NativeKit is experimental and its pre-1.0 ABI is still evolving. It is ready
> for exploration and integration work, not yet for compatibility-critical
> production deployments.

## ✨ See it in action

| NativeKit Lab | OpenGL surface |
|:---:|:---:|
| [![NativeKit Lab](docs/images/nativekit-showcase.png)](docs/images/nativekit-showcase.png) | [![NativeKit OpenGL triangle](docs/images/nativekit-opengl.png)](docs/images/nativekit-opengl.png) |
| Dialogs, clipboard, notifications, windows, WebViews, and a live event log | A responsive native graphics surface rendered through the C API |

These are real captures from the Linux samples. NativeKit uses the corresponding
native services on Windows, macOS, and Android.

## 🚀 Why NativeKit?

- **Small C boundary** — straightforward to call from C, C++, Haxe, Rust, and
  other FFI-capable languages.
- **Native where it matters** — Win32 and WebView2, GTK and WebKitGTK, Cocoa and
  WKWebView, and Android platform views.
- **One asynchronous model** — dialogs, WebViews, clipboard reads,
  notifications, drops, and lifecycle changes arrive through one event queue.
- **Safe opaque handles** — generation-checked handles reject stale resources.
- **Graphics-ready** — OpenGL, OpenGL ES, and Vulkan presentation surfaces,
  with explicit capability discovery.
- **Interop-friendly** — desktop applications can export native window
  descriptors when they need a platform escape hatch.

## 🗺️ Platform feature matrix

The table follows the capabilities advertised by each backend through
`nk_get_capabilities()`. Applications should always query that function at
runtime: optional system components and build configuration can still affect
availability.

| Capability | Linux | Windows | macOS | Android |
|---|:---:|:---:|:---:|:---:|
| NativeKit-owned top-level windows | ✅ | ✅ | ✅ | — |
| Mobile host / caller-owned view attachment | — | — | — | ✅ |
| WebView | ⚙️ | ⚙️ | ✅ | ✅ |
| File and directory dialogs | ✅ | ✅ | ✅ | ✅ |
| Clipboard | ✅ | ✅ | ✅ | ✅ |
| File/resource drag and drop | ✅ | ✅ | ✅ | ✅ |
| Shell and external URL opening | ✅ | ✅ | ✅ | ✅ |
| Locale and desktop appearance | ✅ | ✅ | ✅ | ✅ |
| Desktop notifications | ✅ | ✅ | ✅ | ✅ |
| Export native window descriptor | ✅ | ✅ | ✅ | — |
| Wrap an externally owned native window | — | — | — | — |
| Keyboard, pointer, and text input | ✅ | — | — | ✅ |
| Custom cursors | ✅ | — | — | — |
| Pointer capture | ✅ | — | — | — |
| Extended window geometry | ✅ | — | — | — |
| Extended window styling | ✅ | — | — | — |
| Monitor enumeration | ✅ | — | — | — |
| Monitor/fullscreen mode control | ✅ | — | — | — |
| Joysticks/gamepads | ✅ | — | — | ✅ |
| OpenGL surfaces | ✅ | — | — | — |
| OpenGL ES surfaces | ✅ | — | — | ✅ |
| Vulkan surfaces | ✅ | — | — | ✅ |
| URI resource streams | ✅ | ✅ | ✅ | ✅ |
| Platform resource sharing | — | — | — | ✅ |
| Custom-surface accessibility | — | — | — | ✅ |

**Legend:** ✅ advertised by the backend · ⚙️ requires an optional runtime or
build dependency · — not currently advertised

- Linux desktop support requires GTK 3 and WebKitGTK 4.1. Without them, the
  library builds with a stub backend and reports the services as unsupported.
- Windows WebViews require the Microsoft Edge WebView2 Evergreen Runtime.
  The pinned WebView2 SDK is fetched at configure time unless
  `-DNK_ENABLE_WEBVIEW2=OFF` is used.
- Android attaches to a caller-owned `ViewGroup`; it deliberately does not
  create or own an `Activity` or desktop-style top-level window.
- Capability bits describe complete API groups. Some common window operations
  are available on desktop backends even where the broader extended geometry or
  styling groups are not advertised.

## 🏗️ Build from source

Clone with the vendored wxWidgets donor source initialized:

```sh
git clone --recurse-submodules <nativekit-url>
cd nativekit
```

For an existing checkout:

```sh
git submodule update --init --recursive
```

Configure, build, and test a desktop build with CMake:

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The Android library, sample applications, and Gradle wrapper live under
[`android/`](android/). See the [Android guide](android/README.md) for SDK/NDK,
host lifecycle, and instrumentation-test instructions.

## 🎮 Samples

After a desktop build, try:

```sh
# Embedded HTML, responsive WebView bounds, and page-to-native JSON
./build/examples/nativekit_hello

# Navigation policy, page titles, failures, and an optional starting URL
./build/examples/nativekit_browser https://example.com

# Interactive tour of the desktop API
./build/examples/nativekit_showcase

# Responsive OpenGL triangle
./build/examples/nativekit_opengl

# Vulkan device, swapchain, pipeline, and resize handling
./build/examples/nativekit_vulkan
```

The Vulkan sample is built when Vulkan development files and `glslc` are
available. Both graphics samples accept `--smoke-test`; they render 30 frames,
exercise resize handling, and exit. CTest runs them under Xvfb when available.

## 🧭 API model

NativeKit intentionally stays below the widget-toolkit layer:

- UI APIs are main-thread-only unless their documentation says otherwise.
- Resources are opaque, generation-checked `nk_handle` values.
- Top-level windows can form a small ownership tree for utility and modal
  windows, but NativeKit does not provide panels, controls, or layout widgets.
- WebView creation may be asynchronous. Calls issued before
  `NK_EVENT_WEBVIEW_READY` are retained in order.
- Navigation policy is opt-in; proposed navigations become request events that
  the application explicitly allows or cancels.
- JavaScript results and page messages are compact UTF-8 JSON on every backend.
- Event payloads returned by `nk_poll_event()` must be released with
  `nk_event_release()`.

The public headers in [`include/`](include/) are the normative API reference.
See [the API guide](docs/api.md) for threading, ownership, event payloads, URI
resources, text input, graphics, and accessibility. The latest consistency and
ABI audit is recorded in [the API review](docs/api-review.md).

## 🧪 Testing

The normal CTest suite covers the public C ABI, core lifetime rules, and the host
backend. Platform CI additionally covers:

- Linux GUI and graphics tests under Xvfb, plus sanitizer builds.
- Native Windows x64/x86 tests and ARM64 cross-compilation.
- An isolated MinGW/Wine compatibility suite via `tools/test-wine.sh`.
- Native Intel and Apple Silicon macOS builds.
- Android unit and instrumentation tests.
- Generated Haxeon binding drift and runtime smoke tests.

See [the testing guide](docs/testing.md) for prerequisites, exact coverage, and
the distinction between compatibility-layer and authoritative native tests.

## 🧹 Development

Source formatting is defined by `.clang-format`:

```sh
cmake --build build --target format
cmake --build build --target format-check
```

wxWidgets is pinned as a Git submodule for donor-code provenance. The exact
revision and adapted donor files are recorded in
[`tools/upstream-lock.json`](tools/upstream-lock.json); applicable terms are in
[`licenses/wxWidgets.txt`](licenses/wxWidgets.txt).

## 📍 Project status

NativeKit is under active development. The feature matrix documents what the
current backends advertise, while runtime capability checks remain the contract
applications should trust. Contributions, portability reports, and focused
backend tests are welcome.
