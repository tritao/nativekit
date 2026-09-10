# NativeKit Android proof of concept

The Android library attaches NativeKit to a caller-owned `ViewGroup`. It does
not create or own an Activity. `NativeKitHost` forwards lifecycle changes and
the C `nk_webview_*` API creates child Android WebViews inside that container.

Build with an Android SDK/NDK installation:

```sh
cd android
./gradlew :sample:assembleDebug
```

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
Android currently implements URL opening and text clipboard operations; file
clipboard operations remain unsupported. Open, save, and directory dialogs use
the Storage Access Framework and return `content://` URIs.
Notifications use a library-owned channel, permission proxy, and receiver, so
the embedding Activity does not need to forward permission or intent callbacks.
System directory results are app-scoped paths; locale and appearance reflect the
attached host's current Android configuration.
