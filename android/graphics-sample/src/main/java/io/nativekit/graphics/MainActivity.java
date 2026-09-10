package io.nativekit.graphics;

import android.app.Activity;
import android.os.Bundle;
import android.view.Choreographer;
import android.widget.FrameLayout;
import io.nativekit.NativeKitHost;

/** Continuously renders through the portable OpenGL ES surface API. */
public final class MainActivity extends Activity implements Choreographer.FrameCallback {
    static { System.loadLibrary("nativekit_graphics_sample"); }

    private NativeKitHost nativeKit;
    private FrameLayout content;
    private long surface;
    private boolean rendering;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        content = new FrameLayout(this);
        setContentView(content);
        nativeKit = new NativeKitHost(content);
        content.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop,
                                           oldRight, oldBottom) -> {
            float density = getResources().getDisplayMetrics().density;
            int width = Math.max(1, Math.round((right - left) / density));
            int height = Math.max(1, Math.round((bottom - top) / density));
            if (surface == 0)
                surface = nativeCreateSurface(nativeKit.handle(), width, height);
            else
                nativeResizeSurface(surface, width, height);
        });
    }

    @Override
    protected void onResume() {
        super.onResume();
        nativeKit.active();
        rendering = true;
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        if (!rendering)
            return;
        nativeRenderFrame(surface, frameTimeNanos);
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    protected void onPause() {
        rendering = false;
        Choreographer.getInstance().removeFrameCallback(this);
        nativeKit.inactive();
        super.onPause();
    }

    @Override
    protected void onStop() {
        nativeKit.background();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        nativeDestroySurface(surface);
        nativeKit.close();
        super.onDestroy();
    }

    private static native long nativeCreateSurface(long host, int width, int height);
    private static native void nativeResizeSurface(long surface, int width, int height);
    private static native void nativeRenderFrame(long surface, long frameTimeNanos);
    private static native void nativeDestroySurface(long surface);
}
