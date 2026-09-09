# NativeKit Android proof of concept

The Android library attaches NativeKit to a caller-owned `ViewGroup`. It does
not create or own an Activity. `NativeKitHost` forwards lifecycle changes and
the C `nk_webview_*` API creates child Android WebViews inside that container.

Build with an Android SDK/NDK installation:

```sh
cd android
./gradlew :sample:assembleDebug
```

This checkout was validated with JDK 21, Gradle 8.13, SDK Platform 36,
Build Tools 35.0.0, and NDK 30.0.16248370. Set `sdk.dir` in an untracked
`local.properties` or export `ANDROID_HOME` for your local installation.

The native API must be called on the Android main thread. The sample only owns
the host lifecycle; an embedding runtime creates the WebView and polls NativeKit
events through the stable C ABI.
