package io.nativekit.sample;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.webkit.WebView;
import io.nativekit.NativeKitHost;

public final class NativeKitTestActivity extends Activity {
    FrameLayout container;
    NativeKitHost host;

    void evaluateWebView(String script) {
        WebView webView = findWebView(container);
        if (webView == null)
            throw new IllegalStateException("NativeKit WebView was not attached");
        webView.evaluateJavascript(script, null);
    }

    private static WebView findWebView(View view) {
        if (view instanceof WebView)
            return (WebView)view;
        if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup)view;
            for (int index = 0; index < group.getChildCount(); ++index) {
                WebView webView = findWebView(group.getChildAt(index));
                if (webView != null)
                    return webView;
            }
        }
        return null;
    }

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
