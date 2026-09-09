package io.nativekit.consumer;

import android.app.Activity;
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
        probe = nativeProbe(nativeKit.handle());
    }

    public int apiVersion() { return (int)(probe >>> 32); }

    public long webViewHandle() { return probe & 0xffffffffL; }

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
}
