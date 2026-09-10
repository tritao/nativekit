package io.nativekit;

import android.content.Intent;
import android.view.ViewGroup;

/** Lifecycle adapter for a caller-owned Android NativeKit host. */
public final class NativeKitHost implements AutoCloseable {
    static { System.loadLibrary("nativekit"); }

    private long handle;

    public NativeKitHost(ViewGroup container) {
        if (container == null) {
            throw new IllegalArgumentException("container must not be null");
        }
        nativeInitialize();
        handle = nativeAttach(container);
        if (handle == 0) {
            throw new IllegalStateException("NativeKit could not attach to the container");
        }
        NativeKitBridge.observeHost(container, handle);
    }

    public long handle() { return handle; }

    /** Convenience for the sample; runtimes normally call nk_webview_create directly. */
    public long createWebView(int width, int height, String initialUrl) {
        if (handle == 0) {
            throw new IllegalStateException("host is closed");
        }
        return nativeCreateWebView(handle, width, height, initialUrl);
    }

    public void active() { setLifecycle(1); }

    public void inactive() { setLifecycle(2); }

    public void background() { setLifecycle(3); }

    /** Queues URI/text content from a VIEW or SEND intent. */
    public int dispatchIntent(Intent intent) {
        if (handle == 0 || intent == null)
            throw new IllegalArgumentException("host must be open and intent must not be null");
        return nativeDispatchIntent(handle, intent);
    }

    /** Enables or disables URI/text drops onto the host container. */
    public int setDropEnabled(boolean enabled) {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeSetDropEnabled(handle, enabled);
    }

    /** Returns the next queued event, or {@code null} when the queue is empty. */
    public NativeKitEvent pollEvent() {
        if (handle == 0) {
            throw new IllegalStateException("host is closed");
        }
        return nativePollEvent();
    }

    public int openUrl(String url) {
        if (handle == 0 || url == null)
            throw new IllegalArgumentException("host must be open and URL must not be null");
        return nativeOpenUrl(url);
    }

    public int setClipboardText(String text) {
        if (handle == 0 || text == null)
            throw new IllegalArgumentException("host must be open and text must not be null");
        return nativeSetClipboardText(text);
    }

    public long readClipboardText() {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeReadClipboardText();
    }

    public long openFileDialog(String title) { return startFileDialog(1, title, null); }

    public long saveFileDialog(String title, String suggestedName) {
        return startFileDialog(2, title, suggestedName);
    }

    public long selectDirectoryDialog(String title) { return startFileDialog(3, title, null); }

    private long startFileDialog(int kind, String title, String suggestedName) {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeStartFileDialog(handle, kind, title, suggestedName);
    }

    public int cancelDialog(long request) {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeCancelDialog(request);
    }

    public long showNotification(String title, String body) {
        if (handle == 0 || title == null)
            throw new IllegalArgumentException("host must be open and title must not be null");
        return nativeShowNotification(title, body);
    }

    public int closeNotification(long request) {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeCloseNotification(request);
    }

    public String systemDirectory(int kind) {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeSystemDirectory(kind);
    }

    public String systemLocale() {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeSystemLocale();
    }

    /** Low byte is the NativeKit color scheme; bit 8 indicates high contrast. */
    public int systemAppearance() {
        if (handle == 0)
            throw new IllegalStateException("host is closed");
        return nativeSystemAppearance();
    }

    private void setLifecycle(int state) {
        if (handle != 0) {
            nativeSetLifecycle(handle, state);
        }
    }

    @Override
    public void close() {
        if (handle != 0) {
            NativeKitBridge.unobserveHost(handle);
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private static native void nativeInitialize();
    private static native long nativeAttach(ViewGroup container);
    private static native long nativeCreateWebView(long host, int width, int height,
                                                   String initialUrl);
    private static native void nativeSetLifecycle(long handle, int state);
    private static native int nativeDispatchIntent(long handle, Intent intent);
    private static native int nativeSetDropEnabled(long handle, boolean enabled);
    private static native NativeKitEvent nativePollEvent();
    private static native int nativeOpenUrl(String url);
    private static native int nativeSetClipboardText(String text);
    private static native long nativeReadClipboardText();
    private static native long nativeStartFileDialog(long host, int kind, String title,
                                                     String suggestedName);
    private static native int nativeCancelDialog(long request);
    private static native long nativeShowNotification(String title, String body);
    private static native int nativeCloseNotification(long request);
    private static native String nativeSystemDirectory(int kind);
    private static native String nativeSystemLocale();
    private static native int nativeSystemAppearance();
    private static native void nativeDestroy(long handle);
}
