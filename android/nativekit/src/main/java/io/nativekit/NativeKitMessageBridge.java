package io.nativekit;

import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;

/** Event-only fallback bridge for WebView providers without origin-aware message listeners. */
public final class NativeKitMessageBridge {
    private final long handle;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    NativeKitMessageBridge(long handle) {
        this.handle = handle;
    }

    @JavascriptInterface
    public void postMessage(String message) {
        mainHandler.post(() -> NativeKitBridge.nativeOnMessage(handle, message));
    }
}
