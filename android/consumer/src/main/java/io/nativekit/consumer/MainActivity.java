package io.nativekit.consumer;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.FrameLayout;
import io.nativekit.NativeKitHost;

/** Minimal consumer: Java owns lifecycle while native code calls the NativeKit C ABI. */
public final class MainActivity extends Activity {
    static { System.loadLibrary("nativekit_consumer"); }

    private NativeKitHost nativeKit;
    private long probe;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout content = new FrameLayout(this);
        setContentView(content);
        nativeKit = new NativeKitHost(content);
        nativeKit.dispatchIntent(getIntent());
        probe = nativeProbe(nativeKit.handle());
    }

    public int apiVersion() { return (int)(probe >>> 32); }

    public long webViewHandle() { return probe & 0xffffffffL; }

    public int resourceClipboardProbe() { return nativeResourceClipboardProbe(); }

    public int resourceStreamProbe() { return nativeResourceStreamProbe(); }

    public int persistedResourceProbe() { return nativePersistedResourceProbe(); }

    public int incomingShareProbe() { return nativeIncomingShareProbe(); }

    public int incomingViewProbe() { return nativeIncomingViewProbe(); }

    public int dispatchIntentForTest(Intent intent) { return nativeKit.dispatchIntent(intent); }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (nativeKit != null)
            nativeKit.dispatchIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (nativeKit != null)
            nativeKit.active();
    }

    @Override
    protected void onPause() {
        if (nativeKit != null)
            nativeKit.inactive();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (nativeKit != null)
            nativeKit.close();
        super.onDestroy();
    }

    private static native long nativeProbe(long host);
    private static native int nativeResourceClipboardProbe();
    private static native int nativeResourceStreamProbe();
    private static native int nativePersistedResourceProbe();
    private static native int nativeIncomingShareProbe();
    private static native int nativeIncomingViewProbe();
}
