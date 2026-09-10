# NativeKit Android proof of concept

The Android library attaches NativeKit to a caller-owned `ViewGroup`. It does
not create or own an Activity. `NativeKitHost` forwards lifecycle changes and
the C `nk_webview_*` API creates child Android WebViews inside that container.
The portable `nk_surface_*` API creates child `SurfaceView`-backed OpenGL ES
2.0/3.0 or Vulkan presentation surfaces and manages their native-window
lifecycle. Surface loss and recreation are reported explicitly.
Graphics surfaces also receive portable multi-touch, stylus, mouse, keyboard,
text-input, and gamepad events. Android controller IDs remain internal; public
joystick and gamepad events use NativeKit handles.

Build with an Android SDK/NDK installation:

```sh
cd android
./gradlew :sample:assembleDebug
```

`graphics-sample` continuously renders an animated color with the portable
OpenGL ES surface API and handles pause/resume plus native-surface recreation:

```sh
./gradlew :graphics-sample:assembleDebug
```

Android's typed JNI input constants are generated from the public C headers.
After changing an input enum, run `tools/generate-android-input-values.py` from
the repository root. `./gradlew verifyAndroidInputValues` checks for drift.

With an emulator or device connected, run the Java mapping tests and the
end-to-end host/WebView instrumentation test with:

```sh
./gradlew :nativekit:testDebugUnitTest :sample:connectedDebugAndroidTest
```

`consumer` is a standalone NativeKit integration application. It uses a direct Gradle
project dependency and consumes the C ABI through Prefab without Maven:

```sh
./gradlew :consumer:assembleDebug :consumer:connectedDebugAndroidTest
```

This checkout was validated with JDK 21, Gradle 8.13, SDK Platform 36,
Build Tools 35.0.0, and NDK 30.0.16248370. Set `sdk.dir` in an untracked
`local.properties` or export `ANDROID_HOME` for your local installation.

The native API must be called on the Android main thread. Embedding runtimes may
poll through the stable C ABI; Java/Kotlin hosts can use `NativeKitHost.pollEvent()`.
After attaching, pass the Activity's initial intent to
`NativeKitHost.dispatchIntent(getIntent())`, then forward replacements from
`onNewIntent()`. NativeKit queues `ACTION_VIEW` as `NK_EVENT_RESOURCE_OPENED`
and `ACTION_SEND`/`ACTION_SEND_MULTIPLE` as `NK_EVENT_SHARE_RECEIVED`, preserving
`content://` URIs and their read/write grants. Shares may include resources,
text, and a subject. Resource MIME types and display names are resolved through
`ContentResolver`; providers that omit a display name fall back to the final URI
path segment.
Persistable Storage Access Framework grants can be inspected with
`nk_resource_get_persisted_access()` and updated or released with
`nk_resource_set_persisted_access()`.
`nk_mobile_host_set_drop_enabled()` accepts text and `content://` drag data on
the attached container. Drops preserve logical coordinates and URI metadata;
temporary drag permissions remain valid until the host is destroyed.
Forward the Activity's system-back callback to
`NativeKitHost.handleBack(webViewHandle)` for the currently selected WebView.
It consumes back only when WebView history can be navigated, so the Activity can
perform its normal back behavior when it returns false. The same helper can be
used from predictive-back callbacks.
Android currently implements URL opening and text clipboard operations; file
clipboard operations remain unsupported. Open, save, and directory dialogs use
the Storage Access Framework and return `content://` URIs.
Notifications use a library-owned channel, permission proxy, and receiver, so
the embedding Activity does not need to forward permission or intent callbacks.
System directory results are app-scoped paths; locale and appearance reflect the
attached host's current Android configuration.
