# Testing

The normal CTest suite covers the core ABI and the host platform backend. Linux
GUI tests run under Xvfb when available. The public ABI test exercises repeated
initialization, while the GTK integration test leaves an asynchronous clipboard
read outstanding across shutdown and verifies that it cannot enter the next
runtime generation.

On Linux, `linux_notification_failure` runs in an isolated D-Bus session with
no notification daemon. It verifies that an accepted asynchronous request
produces a queue-guaranteed `NK_EVENT_NOTIFICATION_FAILED` instead of hanging.

`NK_ENABLE_SANITIZERS=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer.
Leak detection remains enabled for the core tests. It is disabled only for the
GTK integration process because GTK, Pango, and Fontconfig retain
process-lifetime caches outside NativeKit's ownership.

## Android tests

The Android workflow pins Java 17, API 36, NDK 30.0.16248370, and CMake 3.22.1.
Its build job checks generated JNI constants, runs the library JVM tests, builds
the AAR and samples, and compiles both C++ Prefab consumers in debug and release
configurations. Native builds cover every ABI declared by the Gradle modules.

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
checked-in binding drift fails before compilation.

## Windows compatibility smoke tests

`tools/test-wine.sh` cross-builds a static Windows test suite with MinGW and runs
it under Wine using a fresh temporary prefix. It covers DLL-independent C ABI
loading, Win32 window creation, UTF-8 title conversion, visibility, bounds, DPI,
native descriptor export, message pumping, asynchronous file and message dialog
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

## macOS native tests

The macOS workflow builds and runs the C ABI, core tests, and Cocoa window/system
integration test on Intel and Apple Silicon runners. The integration test checks
pasteboard text and file round trips, drop registration, and WKWebView creation,
HTML navigation, navigation-policy decisions, JavaScript evaluation, and page
messages. Parent destruction with a pending evaluation verifies deterministic
cancellation and suppression of late callbacks. Dialogs and real drag sessions
are compiled but not opened automatically because unattended native UI is not a
reliable CI interaction surface.
