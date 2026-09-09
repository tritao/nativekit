package io.nativekit.sample;

import android.app.Activity;
import android.os.Bundle;
import android.widget.FrameLayout;
import io.nativekit.NativeKitHost;

public final class NativeKitTestActivity extends Activity {
    FrameLayout container;
    NativeKitHost host;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        container = new FrameLayout(this);
        setContentView(container);
        host = new NativeKitHost(container);
    }

    @Override
    protected void onDestroy() {
        if (host != null)
            host.close();
        super.onDestroy();
    }
}
