package io.nativekit;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.RenderProcessGoneDetail;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.annotation.Nullable;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.webkit.WebViewCompat;
import androidx.webkit.WebViewFeature;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/** Package-private JNI implementation. Public consumers use NativeKitHost. */
final class NativeKitBridge {
    // Stable NativeKit categories from nativekit_webview.h. Android error codes are translated
    // here so platform-specific WebViewClient.ERROR_* values never cross the JNI boundary.
    private static final int NK_NAVIGATION_ERROR_OTHER = 0;
    private static final int NK_NAVIGATION_ERROR_REQUEST = 1;
    private static final int NK_NAVIGATION_ERROR_AUTH = 2;
    private static final int NK_NAVIGATION_ERROR_SECURITY = 3;
    private static final int NK_NAVIGATION_ERROR_NOT_FOUND = 4;
    private static final int NK_NAVIGATION_ERROR_CONNECTION = 5;

    private static final Map<Long, ViewGroup> observedHosts = new HashMap<>();

    private NativeKitBridge() {}

    @SuppressLint("SetJavaScriptEnabled")
    static WebView create(ViewGroup parent, long handle, int flags, int x, int y, int width,
                          int height, @Nullable String initialUrl) {
        WebView view = new WebView(parent.getContext());
        view.setTag(io.nativekit.R.id.nativekit_handle, handle);
        view.setBackgroundColor(Color.TRANSPARENT);
        view.getSettings().setJavaScriptEnabled(true);
        view.setVisibility((flags & 2) != 0 ? View.GONE : View.VISIBLE);
        setBounds(view, x, y, width, height);

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

            @Override
            public void onReceivedError(WebView webView, WebResourceRequest request,
                                        WebResourceError error) {
                if (request.isForMainFrame()) {
                    nativeOnNavigationFailed(handle, navigationErrorCategory(error.getErrorCode()),
                                             error.getDescription().toString());
                }
            }

            @Override
            public boolean onRenderProcessGone(WebView webView, RenderProcessGoneDetail detail) {
                detachAfterRendererGone(webView);
                nativeOnRenderProcessGone(handle, detail.didCrash());
                return true;
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

    private static void detachAfterRendererGone(WebView view) {
        ViewGroup parent = (ViewGroup)view.getParent();
        if (parent != null) {
            parent.removeView(view);
        }
        // Android forbids calling any method on the WebView, including destroy(), after this
        // callback. The native side invalidates the handle and ignores delayed callbacks.
    }

    static void show(WebView view, boolean visible) {
        view.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    static void setBounds(WebView view, int x, int y, int width, int height) {
        float density = view.getResources().getDisplayMetrics().density;
        view.setX(Math.round(x * density));
        view.setY(Math.round(y * density));
        view.setLayoutParams(
            new ViewGroup.LayoutParams(Math.round(width * density), Math.round(height * density)));
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
                } else {
                    ((WebView)child).onPause();
                }
            }
        }
    }

    static void observeHost(ViewGroup parent, long handle) {
        observedHosts.put(handle, parent);
        View.OnLayoutChangeListener listener = (view, left, top, right, bottom, oldLeft, oldTop,
                                                oldRight, oldBottom) -> emitGeometry(parent, handle);
        parent.setTag(io.nativekit.R.id.nativekit_layout_listener, listener);
        parent.addOnLayoutChangeListener(listener);
        ViewCompat.setOnApplyWindowInsetsListener(parent, (view, insets) -> {
            emitGeometry(parent, handle, insets);
            return insets;
        });
        parent.post(() -> emitGeometry(parent, handle));
    }

    static void unobserveHost(long handle) {
        ViewGroup parent = observedHosts.remove(handle);
        if (parent == null)
            return;
        Object value = parent.getTag(io.nativekit.R.id.nativekit_layout_listener);
        if (value instanceof View.OnLayoutChangeListener)
            parent.removeOnLayoutChangeListener((View.OnLayoutChangeListener)value);
        ViewCompat.setOnApplyWindowInsetsListener(parent, null);
    }

    private static void emitGeometry(ViewGroup parent, long handle) {
        emitGeometry(parent, handle, ViewCompat.getRootWindowInsets(parent));
    }

    private static void emitGeometry(ViewGroup parent, long handle,
                                     @Nullable WindowInsetsCompat windowInsets) {
        float density = parent.getResources().getDisplayMetrics().density;
        Insets bars = windowInsets == null
            ? Insets.NONE
            : windowInsets.getInsets(WindowInsetsCompat.Type.systemBars());
        Insets keyboard = windowInsets == null
            ? Insets.NONE
            : windowInsets.getInsets(WindowInsetsCompat.Type.ime());
        nativeOnGeometry(handle, Math.round(parent.getWidth() / density),
                         Math.round(parent.getHeight() / density), density,
                         Math.round(bars.left / density), Math.round(bars.top / density),
                         Math.round(bars.right / density), Math.round(bars.bottom / density),
                         Math.round(keyboard.bottom / density));
    }

    private static long viewHandle(WebView view) {
        Object value = view.getTag(io.nativekit.R.id.nativekit_handle);
        return value instanceof Long ? (Long)value : 0;
    }

    static int navigationErrorCategory(int errorCode) {
        switch (errorCode) {
            case WebViewClient.ERROR_UNSUPPORTED_AUTH_SCHEME:
            case WebViewClient.ERROR_AUTHENTICATION:
            case WebViewClient.ERROR_PROXY_AUTHENTICATION:
                return NK_NAVIGATION_ERROR_AUTH;
            case WebViewClient.ERROR_HOST_LOOKUP:
            case WebViewClient.ERROR_CONNECT:
            case WebViewClient.ERROR_IO:
            case WebViewClient.ERROR_TIMEOUT:
                return NK_NAVIGATION_ERROR_CONNECTION;
            case WebViewClient.ERROR_FILE_NOT_FOUND:
                return NK_NAVIGATION_ERROR_NOT_FOUND;
            case WebViewClient.ERROR_FAILED_SSL_HANDSHAKE:
            case WebViewClient.ERROR_UNSAFE_RESOURCE:
                return NK_NAVIGATION_ERROR_SECURITY;
            case WebViewClient.ERROR_UNSUPPORTED_SCHEME:
            case WebViewClient.ERROR_REDIRECT_LOOP:
            case WebViewClient.ERROR_BAD_URL:
            case WebViewClient.ERROR_FILE:
            case WebViewClient.ERROR_TOO_MANY_REQUESTS:
                return NK_NAVIGATION_ERROR_REQUEST;
            case WebViewClient.ERROR_UNKNOWN:
            default:
                return NK_NAVIGATION_ERROR_OTHER;
        }
    }

    private static native void nativeOnMessage(long handle, @Nullable String json);
    private static native void nativeOnNavigated(long handle, @Nullable String url,
                                                 @Nullable String title);
    private static native void nativeOnEvaluation(long handle, long request,
                                                  @Nullable String result, @Nullable String error);
    private static native boolean nativeOnNavigationRequest(long handle, String url);
    private static native void nativeOnNavigationFailed(long handle, int category, String message);
    private static native void nativeOnRenderProcessGone(long handle, boolean crashed);
    private static native void nativeOnGeometry(long handle, int width, int height, float scale,
                                                int insetLeft, int insetTop, int insetRight,
                                                int insetBottom, int keyboardBottom);
}
