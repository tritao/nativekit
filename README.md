# NativeKit 🧰

**Cross-platform native capabilities behind one compact C ABI.**

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
- **Plugin-ready core** — generation-bound plugin lifetimes, numeric service
  dispatch, and bounded binary payloads with handles for bulk data.
- **Graphics-ready** — OpenGL, OpenGL ES, Metal, and Vulkan presentation surfaces,
  with explicit capability discovery and request-driven frame scheduling.
- **Interop-friendly** — desktop applications can export or wrap native window
  descriptors when they need a platform escape hatch.
- **Host native content** — NativeKit-owned child views take pending bounds,
  visibility, and clip state and publish it with one commit, while the
  application owns the platform widget or view they contain.

## 🗺️ Platform feature matrix

This overview groups related API capabilities to keep platform support easy to
scan. Applications should still query `nk_get_capabilities()` at runtime:
optional system components and build configuration can affect availability. The
normative target, current deferred work, and executable capability contract are
maintained in the [platform parity contract](docs/platform-parity.md).

| Feature family | Linux | Windows | macOS | Android | iOS | Web / WASM |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| Windows and lifecycle | ✅ | ✅ | ✅ | Host view | Host view | Partial |
| WebView | ✅ | ✅ | ✅ | ✅ | ✅ | Native DOM |
| URI resource dialogs and system services | ✅ | ✅ | ✅ | ✅ | ✅ | Equivalent |
| Clipboard and drag/drop | ✅ | ✅ | ✅ | ✅ | ✅ | Partial |
| Notifications | ✅ | ✅ | ✅ | ✅ | ✅ | Partial |
| Input, cursors, and capture | ✅ | ✅ | ✅ | Partial | Partial | Partial |
| Monitors and fullscreen modes | ✅ | ✅ | ✅ | — | — | — |
| Joysticks and gamepads | ✅ | ✅ | ✅ | ✅ | ✅ | Partial |
| OpenGL / OpenGL ES | ✅ | — | — | GLES | — | Partial |
| Metal | — | — | ✅ | — | ✅ | — |
| Vulkan | ✅ | — | — | ✅ | 🚧 | 🚧 |
| URI resources and sharing | ✅ | ✅ | ✅ | ✅ | ✅ | Partial |
| Custom-surface accessibility | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Native interoperability | Export + wrap | Export + wrap | Export + wrap | Host view | Host view | — |

**Legend:** ✅ supported · **Partial** a subset is supported · 🚧 coming soon ·
— not currently advertised

- iOS has device compile/link coverage and simulator runtime CI for the core
  library. UIKit host attachment, geometry lifecycle, Metal presentation surface,
  touch/pointer input, hardware-key events, UIKit text editing, WKWebView, resource
  dialogs, message dialogs, clipboard and URI resources, URI opening, sandbox
  directory queries, locale, appearance, notifications, host drag/drop, the iOS
  share sheet, and GameController joystick/gamepad input are available. Custom-surface accessibility is
  projected into UIKit and Cocoa accessibility elements and VoiceOver/Voice Control actions.
- Web custom-surface accessibility mirrors the NativeKit semantic tree into an
  accessible DOM overlay. Focus, activation, keyboard adjustments, text value,
  and selection actions return through the shared accessibility event contract.
- Web shell opening, appearance, notifications, resource pickers, writable save
  streams, and gamepads use browser APIs. Save and directory pickers retain
  opaque browser handles for the lifetime of the NativeKit runtime; open files
  remain temporary `blob:` resources. Pickers and notifications are subject to
  browser permissions and user-activation rules. Window styling uses the
  browser canvas CSS model; page-owned decorations and activation are outside
  the contract.
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
- See the [API guide](docs/api.md) and `NK_CAP_*` declarations in
  [`nativekit_window.h`](include/nativekit_window.h) for exact capability-level
  details.

## 🏗️ Build from source

Clone the repository:

```sh
git clone https://github.com/tritao/nativekit.git
cd nativekit
```

The optional GPU and UI modules use pinned Git submodules. Initialize them
only when building those modules:

```sh
git submodule update --init
```

Configure, build, and test a desktop build with CMake:

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For a Linux Release package with stripped shared libraries and separate debug
symbols, configure and build NativeKit, then run:

```sh
tools/package-native-release.sh build-release out/nativekit
```

The package helper writes debug files under `lib/.debug/` and adds GNU debug
links to the stripped libraries.

The experimental low-level GPU module remains optional so the core platform
library stays compact; enable it with `-DNK_BUILD_GPU=ON`. HTTP networking is
part of the main NativeKit library and is available as a runtime capability.
On Linux, opt into the system libcurl backend with
`-DNK_USE_SYSTEM_CURL=ON`; when curl is unavailable, the API remains present
but reports `NK_ERROR_UNSUPPORTED`. Module architecture and build notes live
under [`modules/`](modules/). The UI framework, browser showcase, and visual
test tooling now live in the sibling `uikit` project.

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
Use the [API documentation audit](docs/api-doc-audit.md) to inventory missing
comments in C declarations and generated HXI surfaces.

## 🧪 Testing

The normal CTest suite covers the public C ABI, core lifetime rules, and the host
backend. Platform CI additionally covers:

- Linux GUI and graphics tests under Xvfb, plus sanitizer builds.
- Native Windows x64/x86 tests and ARM64 cross-compilation.
- An isolated MinGW/Wine compatibility suite via `tools/test-wine.sh`.
- Native Intel and Apple Silicon macOS builds.
- Android unit and instrumentation tests.
- Shared desktop capability conformance, Web browser-equivalent integration,
  and iOS simulator runtime tests.
- Generated Haxeon binding drift and runtime smoke tests.

See [the testing guide](docs/testing.md) for prerequisites, exact coverage, and
the distinction between compatibility-layer and authoritative native tests.

## 🧹 Development

Source formatting is defined by `.clang-format`:

```sh
cmake --build build --target format
cmake --build build --target format-check
```

## 📍 Project status

NativeKit is under active development. The feature matrix documents what the
current backends advertise, while runtime capability checks remain the contract
applications should trust. Contributions, portability reports, and focused
backend tests are welcome.

## 📄 License

NativeKit is available under the [Apache License 2.0](LICENSE). Third-party
components retain their respective licenses.
