# Testing

The normal CTest suite covers the core ABI and the host platform backend. Linux
GUI tests run under Xvfb when available. The public ABI test exercises repeated
initialization, while the GTK integration test leaves an asynchronous clipboard
read outstanding across shutdown and verifies that it cannot enter the next
runtime generation.

## Capability contract and shared conformance

`platform_parity` loads the checked-in capability snapshot at
`tests/capability-snapshots.txt`. It validates every backend row's classification
and verifies that the running backend advertises all required capabilities,
without deferred or not-applicable bits. An intentional capability change must
update the snapshot, the normative
[platform parity contract](platform-parity.md), and behavior coverage together.

Desktop backends also run `capability_conformance`, a shared operation-level
suite for windows, extended geometry and styling, input/IME, cursors, monitors,
resource I/O and sharing, graphics surfaces, accessibility, system services,
and WebView operations. Linux runs it under Xvfb; Windows and macOS run the
same executable natively. The suite is capability-driven so platform-specific
equivalents can share semantic coverage without requiring identical native
objects.

`platform_parity` validates every row in the snapshot on each native build, then
compares the running backend's advertised mask with its own row. The Linux CI
contract job also runs the shared capability conformance suite, while the
Windows and macOS jobs run that suite through their native CTest matrices.

On Linux, `linux_notification_failure` runs in an isolated D-Bus session with
no notification daemon. It verifies that an accepted asynchronous request
produces a queue-guaranteed `NK_EVENT_NOTIFICATION_FAILED` instead of hanging.

## Stabilization gate

Until the CI matrix is green, defer new subsystem abstractions. Keep work
focused on generated-binding drift, configure/build/test failures, and validating
the existing renderer contract in
[ADR 0010](decisions/0010-explicit-rendering-backends.md). The gate covers
Windows D3D11 (x64, Win32, and ARM64), macOS Metal (Intel and Apple Silicon),
Android API 23 and 36, and the Web smoke, visual, benchmark, and profiling
checks.

`NK_ENABLE_SANITIZERS=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer.
Leak detection remains enabled for the core tests. It is disabled only for the
GTK integration process because GTK, Pango, and Fontconfig retain
process-lifetime caches outside NativeKit's ownership.

## Web accessibility test

When Emscripten tests are enabled, `nativekit_web_accessibility` produces a
browser-hosted HTML integration binary. It publishes a small semantic tree,
checks focus and text-range updates, dispatches a DOM activation, and verifies
that the resulting `NK_EVENT_ACCESSIBILITY_ACTION` reaches NativeKit. Serve
the generated HTML over HTTP and run it in the same browser environment used by
the Web smoke tests. `tools/build-web.sh` builds this artifact and
`tools/test-web.sh` runs it automatically when the test artifact is present.

## Web system-equivalents test

When Emscripten tests are enabled, `nativekit_web_system_equivalents` is a
browser-hosted compile/link and smoke artifact. It checks the advertised shell,
appearance, notification, joystick, and resource-I/O capabilities, validates
the appearance and URI input boundaries, performs an initial Gamepad
enumeration, and exercises a writable retained-handle stream through a fake
File System Access handle. The asynchronous flush is marked on the document
for browser-runner assertions. `tools/test-web.sh` checks that marker through
the same Chrome DevTools session as the showcase smoke test. Resource pickers
and notification permission prompts still require a user-activated browser test
because browsers
intentionally reject those APIs outside a trusted user gesture.

## Android tests

The Android workflow pins Java 17, API 36, NDK 30.0.16248370, and CMake 3.22.1.
Its build job checks generated JNI input and accessibility values, runs the
library JVM tests, builds the AAR and samples, and compiles both C++ Prefab
consumers in debug and release configurations. Native builds cover every ABI
declared by the Gradle modules.

Instrumentation runs on API 23 (the supported minimum) and API 36. The sample
tests exercise host and WebView integration; the consumer tests additionally
cover URI providers, clipboard and sharing, lifecycle recovery, graphics
surfaces, and input delivery. Run the same checks locally with:

```sh
cd android
./gradlew verifyAndroidInputValues :nativekit:testDebugUnitTest \
  :nativekit:assembleRelease :sample:assembleDebug \
  :consumer:assembleDebugAndroidTest :consumer:assembleRelease \
  :graphics-sample:assembleRelease
./gradlew :sample:connectedDebugAndroidTest :consumer:connectedDebugAndroidTest
```

## Haxeon binding smoke test

`tools/test-haxeon.sh` builds a temporary shared NativeKit library, projects the
reviewed Linux HXI declarations into Haxe, and runs the result through HashLink.
It covers initialization, typed event polling, UTF-8 diagnostics, window and
monitor handles, and the two-call monitor-name output buffer. The sibling
`../realtime-haxe` checkout is used by default; set `HAXEON_DIR` to override it.
The smoke test first runs `tools/update-haxeon-hxi.sh --check`, so header and
checked-in binding drift fails before compilation. The UI projection is composed
with the core `NativeKit` HXI, so core declarations are projected once and the
UI interface owns only its `nkui_*` functions.

## Windows compatibility smoke tests

`tools/test-wine.sh` cross-builds a static Windows test suite with MinGW and runs
it under Wine using a fresh temporary prefix. It covers DLL-independent C ABI
loading, Win32 window creation, UTF-8 title conversion, visibility, bounds, DPI,
native descriptor export and borrowed-window wrapping/destruction, message pumping,
asynchronous file and message dialog
cancellation, known-folder and locale queries, appearance detection, shell input
validation, Unicode clipboard text/file round trips, synthesized shell file drops,
destruction, and stale handles.

Required commands are `x86_64-w64-mingw32-gcc`,
`x86_64-w64-mingw32-g++`, `wine`, `wineserver`, `cmake`, `ninja`, and `xvfb-run`.

```sh
tools/test-wine.sh
```

The Wine suite is a compatibility layer, not authoritative Windows validation.
It validates WebView2's runtime-unavailable behavior. The x64 and x86 native
Windows jobs exercise queued creation, readiness, navigation, title changes,
navigation-policy decisions, native messages, JavaScript evaluation, and
destruction during initialization when the Evergreen runtime is present; ARM64
is cross-compiled. Native testing
remains authoritative for COM, accessibility, per-monitor DPI, system
integration, and browser behavior.

## iOS simulator runtime tests

The iOS workflow builds the library for device and simulator SDKs. The
simulator job additionally builds `nativekit_ios_runtime_tests`, installs it on
an available iPhone simulator, and launches the app through `simctl`. The app
attaches a real UIKit host and exercises capability discovery, host drop
registration, Metal frame acquisition and callbacks, UIKit text input,
custom-surface accessibility, WKWebView evaluation, appearance, text and URI
clipboard round trips, the iOS share sheet, and GameController enumeration
before reporting a pass/fail marker. This keeps the device build as a
compile/link check while giving the simulator backend runtime coverage for its
advertised mobile capabilities.

## macOS native tests

The macOS workflow builds and runs the C ABI, core tests, and Cocoa window/system
integration test on Intel and Apple Silicon runners. The integration test checks
pasteboard text and file round trips, drop registration, and WKWebView creation,
HTML navigation, navigation-policy decisions, JavaScript evaluation, and page
messages. Parent destruction with a pending evaluation verifies deterministic
cancellation and suppression of late callbacks. Dialogs and real drag sessions
are compiled but not opened automatically because unattended native UI is not a
reliable CI interaction surface.
