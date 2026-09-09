package io.nativekit;

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

    private void setLifecycle(int state) {
        if (handle != 0) {
            nativeSetLifecycle(handle, state);
        }
    }

    @Override
    public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private static native void nativeInitialize();
    private static native long nativeAttach(ViewGroup container);
    private static native long nativeCreateWebView(long host, int width, int height,
                                                   String initialUrl);
    private static native void nativeSetLifecycle(long handle, int state);
    private static native void nativeDestroy(long handle);
}
