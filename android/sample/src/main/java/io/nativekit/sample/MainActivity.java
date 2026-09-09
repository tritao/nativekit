package io.nativekit.sample;

import android.app.Activity;
import android.os.Bundle;
import android.widget.FrameLayout;
import io.nativekit.NativeKitHost;

public final class MainActivity extends Activity {
    private NativeKitHost nativeKit;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout content = new FrameLayout(this);
        setContentView(content);
        nativeKit = new NativeKitHost(content);
        content.post(
            ()
                -> nativeKit.createWebView(
                    content.getWidth(), content.getHeight(),
                    "data:text/html,<meta name=viewport content='width=device-width'>"
                        + "<h1>NativeKit Android</h1><p>Native host and WebView attached.</p>"));
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
    protected void onStop() {
        if (nativeKit != null)
            nativeKit.background();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        if (nativeKit != null)
            nativeKit.close();
        super.onDestroy();
    }
}
