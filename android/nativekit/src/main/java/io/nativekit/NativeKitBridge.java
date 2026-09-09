package io.nativekit;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.annotation.Nullable;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;
import java.util.Collections;

/** Package-private JNI implementation. Public consumers use NativeKitHost. */
final class NativeKitBridge {
    private NativeKitBridge() {}

    @SuppressLint("SetJavaScriptEnabled")
    static WebView create(ViewGroup parent, long handle, int flags, int x, int y, int width,
                          int height, @Nullable String initialUrl) {
        WebView view = new WebView(parent.getContext());
        view.setTag(io.nativekit.R.id.nativekit_handle, handle);
        view.setBackgroundColor(Color.TRANSPARENT);
        view.getSettings().setJavaScriptEnabled(true);
        view.setVisibility((flags & 2) != 0 ? View.GONE : View.VISIBLE);
        view.setX(x);
        view.setY(y);
        view.setLayoutParams(new ViewGroup.LayoutParams(width, height));

        if (!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            throw new UnsupportedOperationException(
                "Android System WebView lacks WebMessageListener");
        }
        WebViewCompat.addWebMessageListener(
            view, "nativekitBridge", Collections.singleton("*"),
            (webView, message, sourceOrigin, isMainFrame, replyProxy) -> {
                if (isMainFrame)
                    nativeOnMessage(handle, message.getData());
            });

        if (WebViewFeature.isFeatureSupported(WebViewFeature.DOCUMENT_START_SCRIPT)) {
            WebViewCompat.addDocumentStartJavaScript(view, pageBridgeScript(),
                                                     Collections.singleton("*"));
        }

        view.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView webView, String url) {
                installPageBridge(webView);
                nativeOnNavigated(handle, url, webView.getTitle());
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView webView, WebResourceRequest request) {
                String url = request.getUrl().toString();
                return nativeOnNavigationRequest(handle, url);
            }
        });
        parent.addView(view);
        installPageBridge(view);
        if (initialUrl != null) {
            view.loadUrl(initialUrl);
        }
        return view;
    }

    private static void installPageBridge(WebView view) {
        view.evaluateJavascript(pageBridgeScript(), null);
    }

    private static String pageBridgeScript() {
        return "window.webkit=window.webkit||{};"
            + "window.webkit.messageHandlers=window.webkit.messageHandlers||{};"
            + "window.webkit.messageHandlers.nativekit={postMessage:function(v){"
            + "nativekitBridge.postMessage(JSON.stringify(v));}};";
    }

    static void destroy(WebView view) {
        ViewGroup parent = (ViewGroup)view.getParent();
        if (parent != null) {
            parent.removeView(view);
        }
        view.stopLoading();
        view.destroy();
    }

    static void show(WebView view, boolean visible) {
        view.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    static void setBounds(WebView view, int x, int y, int width, int height) {
        view.setX(x);
        view.setY(y);
        view.setLayoutParams(new ViewGroup.LayoutParams(width, height));
    }

    static void navigate(WebView view, String url, boolean bypassPolicy) { view.loadUrl(url); }

    static void setHtml(WebView view, String html, @Nullable String baseUrl) {
        view.loadDataWithBaseURL(baseUrl, html, "text/html", "UTF-8", null);
    }

    static void evaluate(WebView view, long request, String script) {
        view.evaluateJavascript(
            script, result -> nativeOnEvaluation(viewHandle(view), request, result, null));
    }

    static void setLifecycle(ViewGroup parent, int state) {
        for (int i = 0; i < parent.getChildCount(); ++i) {
            View child = parent.getChildAt(i);
            if (child instanceof WebView) {
                if (state == 1) {
                    ((WebView)child).onResume();
                    ((WebView)child).resumeTimers();
                } else {
                    ((WebView)child).onPause();
                    ((WebView)child).pauseTimers();
                }
            }
        }
    }

    private static long viewHandle(WebView view) {
        Object value = view.getTag(io.nativekit.R.id.nativekit_handle);
        return value instanceof Long ? (Long)value : 0;
    }

    private static native void nativeOnMessage(long handle, @Nullable String json);
    private static native void nativeOnNavigated(long handle, @Nullable String url,
                                                 @Nullable String title);
    private static native void nativeOnEvaluation(long handle, long request,
                                                  @Nullable String result, @Nullable String error);
    private static native boolean nativeOnNavigationRequest(long handle, String url);
}
