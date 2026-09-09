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

This checkout was validated with JDK 21, Gradle 8.13, SDK Platform 36,
Build Tools 35.0.0, and NDK 30.0.16248370. Set `sdk.dir` in an untracked
`local.properties` or export `ANDROID_HOME` for your local installation.

The native API must be called on the Android main thread. Embedding runtimes may
poll through the stable C ABI; Java/Kotlin hosts can use `NativeKitHost.pollEvent()`.
Android currently implements URL opening and text clipboard operations; file
clipboard operations remain unsupported.
