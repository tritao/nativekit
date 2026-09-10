package io.nativekit;

import static io.nativekit.NativeKitInputValues.*;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.content.ContextWrapper;
import android.hardware.input.InputManager;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.view.View;
import android.view.ViewGroup;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.text.InputType;
import android.view.DragAndDropPermissions;
import android.view.DragEvent;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.HashSet;
import java.util.Set;
import java.util.Locale;

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
    private static final Map<Long, ArrayList<DragAndDropPermissions>> dropPermissions =
        new HashMap<>();
    private static final Set<Long> cancelledDialogs = new HashSet<>();
    private static InputManager inputManager;
    private static InputManager.InputDeviceListener inputDeviceListener;

    private NativeKitBridge() {}

    private static final class NativeSurfaceView extends SurfaceView {
        private final long nativeHandle;

        NativeSurfaceView(Context context, long handle) {
            super(context);
            nativeHandle = handle;
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            requestFocus();
            int masked = event.getActionMasked();
            int changed = event.getActionIndex();
            if (masked == MotionEvent.ACTION_MOVE || masked == MotionEvent.ACTION_CANCEL) {
                for (int index = 0; index < event.getPointerCount(); ++index)
                    emitTouch(event, index, masked);
            } else {
                emitTouch(event, changed, masked);
            }
            return true;
        }

        private void emitTouch(MotionEvent event, int index, int action) {
            int tool = event.getToolType(index);
            if (tool == MotionEvent.TOOL_TYPE_MOUSE) {
                nativeOnPointerMove(nativeHandle, logical(event.getX(index)),
                                    logical(event.getY(index)));
                return;
            }
            @TouchAction int nativeAction = action == MotionEvent.ACTION_DOWN ||
                                       action == MotionEvent.ACTION_POINTER_DOWN
                                   ? TOUCH_BEGIN
                                   : action == MotionEvent.ACTION_MOVE ? TOUCH_MOVE
                                   : action == MotionEvent.ACTION_UP ||
                                             action == MotionEvent.ACTION_POINTER_UP ? TOUCH_END
                                                                                     : TOUCH_CANCEL;
            @TouchTool int nativeTool = tool == MotionEvent.TOOL_TYPE_STYLUS
                                            ? TOUCH_TOOL_STYLUS
                                            : tool == MotionEvent.TOOL_TYPE_ERASER
                                                  ? TOUCH_TOOL_ERASER
                                                  : TOUCH_TOOL_FINGER;
            float orientation = event.getOrientation(index);
            float tilt = event.getAxisValue(MotionEvent.AXIS_TILT, index);
            nativeOnTouch(nativeHandle, event.getPointerId(index), nativeAction, nativeTool,
                          logical(event.getX(index)), logical(event.getY(index)),
                          event.getPressure(index),
                          (float)(Math.sin(orientation) * tilt),
                          (float)(Math.cos(orientation) * tilt), modifiers(event.getMetaState()));
        }

        @Override
        public boolean onGenericMotionEvent(MotionEvent event) {
            int source = event.getSource();
            if ((source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
                registerGamepad(event.getDevice(), event.getDeviceId());
                int[] axes = {MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z,
                              MotionEvent.AXIS_RZ, MotionEvent.AXIS_LTRIGGER,
                              MotionEvent.AXIS_RTRIGGER};
                for (int index = 0; index < axes.length; ++index)
                    nativeOnGamepadAxis(nativeHandle, event.getDeviceId(), index,
                                        event.getAxisValue(axes[index]));
                return true;
            }
            if ((source & InputDevice.SOURCE_CLASS_POINTER) != 0) {
                if (event.getActionMasked() == MotionEvent.ACTION_HOVER_ENTER ||
                    event.getActionMasked() == MotionEvent.ACTION_HOVER_EXIT)
                    nativeOnPointerEnter(nativeHandle,
                        event.getActionMasked() == MotionEvent.ACTION_HOVER_ENTER);
                nativeOnPointerMove(nativeHandle, logical(event.getX()), logical(event.getY()));
                if (event.getActionMasked() == MotionEvent.ACTION_SCROLL)
                    nativeOnPointerScroll(nativeHandle,
                        event.getAxisValue(MotionEvent.AXIS_HSCROLL),
                        event.getAxisValue(MotionEvent.AXIS_VSCROLL));
                if (event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS ||
                    event.getActionMasked() == MotionEvent.ACTION_BUTTON_RELEASE) {
                    int actionButton = event.getActionButton();
                    if (actionButton == 0)
                        actionButton = event.getButtonState();
                    nativeOnPointerButton(nativeHandle, pointerButton(actionButton),
                        event.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS,
                        modifiers(event.getMetaState()), logical(event.getX()),
                        logical(event.getY()));
                }
                return true;
            }
            return super.onGenericMotionEvent(event);
        }

        @Override
        public boolean onKeyDown(int keyCode, KeyEvent event) {
            return emitKey(keyCode, event,
                           event.getRepeatCount() == 0 ? INPUT_PRESS : INPUT_REPEAT);
        }

        @Override
        public boolean onKeyUp(int keyCode, KeyEvent event) {
            return emitKey(keyCode, event, INPUT_RELEASE);
        }

        private boolean emitKey(int keyCode, KeyEvent event, @InputAction int action) {
            boolean controller = (event.getSource() & InputDevice.SOURCE_GAMEPAD) ==
                                     InputDevice.SOURCE_GAMEPAD ||
                                 (event.getSource() & InputDevice.SOURCE_JOYSTICK) ==
                                     InputDevice.SOURCE_JOYSTICK;
            int gamepad = controller ? gamepadButton(keyCode) : -1;
            if (gamepad >= 0) {
                registerGamepad(event.getDevice(), event.getDeviceId());
                nativeOnGamepadButton(nativeHandle, event.getDeviceId(), gamepad, action != 0);
                return true;
            }
            nativeOnKey(nativeHandle, nativeKey(keyCode), event.getScanCode(), action,
                        modifiers(event.getMetaState()));
            int unicode = event.getUnicodeChar();
            if (action == INPUT_PRESS && unicode != 0)
                nativeOnText(nativeHandle, unicode);
            return true;
        }

        @Override
        public boolean onCheckIsTextEditor() { return true; }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo attributes) {
            attributes.inputType = InputType.TYPE_CLASS_TEXT |
                                   InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            attributes.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return new BaseInputConnection(this, false) {
                @Override
                public boolean commitText(CharSequence text, int cursor) {
                    text.codePoints().forEach(value -> nativeOnText(nativeHandle, value));
                    return true;
                }
            };
        }

        private float logical(float value) {
            return value / getResources().getDisplayMetrics().density;
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    static WebView create(ViewGroup parent, long handle, int flags, int x, int y, int width,
                          int height, @Nullable String initialUrl) {
        WebView view = new WebView(parent.getContext());
        view.setTag(io.nativekit.R.id.nativekit_handle, handle);
        view.setBackgroundColor(Color.TRANSPARENT);
        view.getSettings().setJavaScriptEnabled(true);
        view.setVisibility((flags & 2) != 0 ? View.GONE : View.VISIBLE);
        parent.addView(view);
        setBounds(view, x, y, width, height);
        parent.removeView(view);

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

    static SurfaceView createSurface(ViewGroup parent, long handle, int flags, int x, int y,
                                     int width, int height) {
        SurfaceView view = new NativeSurfaceView(parent.getContext(), handle);
        view.setTag(io.nativekit.R.id.nativekit_handle, handle);
        if ((flags & 2) != 0) {
            view.setZOrderOnTop(true);
            view.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        }
        view.setVisibility((flags & 1) != 0 ? View.GONE : View.VISIBLE);
        parent.addView(view);
        setBounds(view, x, y, width, height);
        parent.removeView(view);
        view.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(SurfaceHolder holder) {
                nativeOnSurfaceCreated(handle, holder.getSurface());
            }

            @Override
            public void surfaceChanged(SurfaceHolder holder, int format, int framebufferWidth,
                                       int framebufferHeight) {
                float density = view.getResources().getDisplayMetrics().density;
                nativeOnSurfaceChanged(handle, Math.round(framebufferWidth / density),
                                       Math.round(framebufferHeight / density), framebufferWidth,
                                       framebufferHeight);
            }

            @Override
            public void surfaceDestroyed(SurfaceHolder holder) {
                nativeOnSurfaceDestroyed(handle);
            }
        });
        parent.addView(view);
        return view;
    }

    static void destroySurface(SurfaceView view) {
        ViewGroup parent = (ViewGroup)view.getParent();
        if (parent != null)
            parent.removeView(view);
    }

    static void showSurface(SurfaceView view, boolean visible) {
        view.setVisibility(visible ? View.VISIBLE : View.GONE);
    }

    static void setSurfaceBounds(SurfaceView view, int x, int y, int width, int height) {
        setBounds(view, x, y, width, height);
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

    static void setBounds(View view, int x, int y, int width, int height) {
        float density = view.getResources().getDisplayMetrics().density;
        view.setX(Math.round(x * density));
        view.setY(Math.round(y * density));
        ViewGroup.LayoutParams layout = view.getLayoutParams();
        if (layout == null)
            layout = new ViewGroup.LayoutParams(0, 0);
        layout.width = Math.round(width * density);
        layout.height = Math.round(height * density);
        view.setLayoutParams(layout);
    }

    static void navigate(WebView view, String url, boolean bypassPolicy) { view.loadUrl(url); }

    static void setHtml(WebView view, String html, @Nullable String baseUrl) {
        view.loadDataWithBaseURL(baseUrl, html, "text/html", "UTF-8", null);
    }

    static boolean canGoBack(WebView view) { return view.canGoBack(); }

    static boolean canGoForward(WebView view) { return view.canGoForward(); }

    static void goBack(WebView view) { view.goBack(); }

    static void goForward(WebView view) { view.goForward(); }

    static void reload(WebView view) { view.reload(); }

    static void stop(WebView view) { view.stopLoading(); }

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

    static boolean dispatchIntent(ViewGroup parent, long host, Intent intent) {
        String action = intent.getAction();
        boolean view = Intent.ACTION_VIEW.equals(action);
        boolean share = Intent.ACTION_SEND.equals(action) || Intent.ACTION_SEND_MULTIPLE.equals(action);
        if (!view && !share)
            return false;
        ArrayList<Uri> uris = new ArrayList<>();
        if (view && intent.getData() != null)
            addUniqueUri(uris, intent.getData());
        ClipData clips = intent.getClipData();
        if (clips != null) {
            for (int index = 0; index < clips.getItemCount(); ++index)
                addUniqueUri(uris, clips.getItemAt(index).getUri());
        }
        if (share) {
            if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
                ArrayList<Uri> streams = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (streams != null)
                    for (Uri uri : streams)
                        addUniqueUri(uris, uri);
            } else {
                addUniqueUri(uris, intent.getParcelableExtra(Intent.EXTRA_STREAM));
            }
        }
        if (view && uris.isEmpty())
            return false;
        String[] values = new String[uris.size()];
        String[] mimeTypes = new String[uris.size()];
        String[] displayNames = new String[uris.size()];
        int[] resourceFlags = new int[uris.size()];
        int grants = intent.getFlags();
        int flags = (grants & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0 ? 1 : 0;
        if ((grants & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0)
            flags |= 2;
        for (int index = 0; index < uris.size(); ++index) {
            values[index] = uris.get(index).toString();
            mimeTypes[index] = resourceMimeType(parent.getContext(), uris.get(index),
                                                intent.getType());
            displayNames[index] = resourceDisplayName(parent.getContext(), uris.get(index));
            resourceFlags[index] = flags;
        }
        CharSequence text = share ? intent.getCharSequenceExtra(Intent.EXTRA_TEXT) : null;
        CharSequence subject = share ? intent.getCharSequenceExtra(Intent.EXTRA_SUBJECT) : null;
        nativeOnIncomingIntent(host, view ? 1 : 2, text == null ? null : text.toString(),
                               subject == null ? null : subject.toString(), values, mimeTypes,
                               displayNames, resourceFlags);
        return true;
    }

    static void setDropEnabled(ViewGroup parent, long host, boolean enabled) {
        if (!enabled) {
            parent.setOnDragListener(null);
            releaseDropPermissions(host);
            return;
        }
        parent.setOnDragListener((view, event) -> {
            if (event.getAction() == DragEvent.ACTION_DRAG_STARTED)
                return event.getClipDescription() != null;
            if (event.getAction() != DragEvent.ACTION_DROP)
                return true;
            ClipData clips = event.getClipData();
            if (clips == null)
                return false;
            Activity activity = activity(parent.getContext());
            if (activity != null && Build.VERSION.SDK_INT >= 24) {
                DragAndDropPermissions permissions = activity.requestDragAndDropPermissions(event);
                if (permissions != null)
                    dropPermissions.computeIfAbsent(host, ignored -> new ArrayList<>())
                        .add(permissions);
            }
            ArrayList<Uri> uris = new ArrayList<>();
            StringBuilder text = new StringBuilder();
            for (int index = 0; index < clips.getItemCount(); ++index) {
                ClipData.Item item = clips.getItemAt(index);
                addUniqueUri(uris, item.getUri());
                if (item.getText() != null) {
                    if (text.length() != 0)
                        text.append('\n');
                    text.append(item.getText());
                }
            }
            if (uris.isEmpty() && text.length() == 0)
                return false;
            String[] values = new String[uris.size()];
            String[] mimeTypes = new String[uris.size()];
            String[] displayNames = new String[uris.size()];
            int[] flags = new int[uris.size()];
            String fallbackMime = clips.getDescription().getMimeTypeCount() == 1
                ? clips.getDescription().getMimeType(0)
                : null;
            for (int index = 0; index < uris.size(); ++index) {
                Uri uri = uris.get(index);
                values[index] = uri.toString();
                mimeTypes[index] = resourceMimeType(parent.getContext(), uri, fallbackMime);
                displayNames[index] = resourceDisplayName(parent.getContext(), uri);
                flags[index] = 1;
            }
            float density = parent.getResources().getDisplayMetrics().density;
            nativeOnResourceDrop(host, event.getX() / density, event.getY() / density,
                                 text.length() == 0 ? null : text.toString(), values, mimeTypes,
                                 displayNames, flags);
            return true;
        });
    }

    private static void releaseDropPermissions(long host) {
        ArrayList<DragAndDropPermissions> permissions = dropPermissions.remove(host);
        if (permissions != null && Build.VERSION.SDK_INT >= 24)
            for (DragAndDropPermissions permission : permissions)
                permission.release();
    }

    @Nullable
    private static Activity activity(Context context) {
        while (context instanceof ContextWrapper) {
            if (context instanceof Activity)
                return (Activity)context;
            Context next = ((ContextWrapper)context).getBaseContext();
            if (next == context)
                break;
            context = next;
        }
        return context instanceof Activity ? (Activity)context : null;
    }

    private static void addUniqueUri(ArrayList<Uri> uris, @Nullable Uri uri) {
        if (uri != null && !uris.contains(uri))
            uris.add(uri);
    }

    @Nullable
    static String resourceMimeType(Context context, Uri uri, @Nullable String fallback) {
        try {
            String value = context.getContentResolver().getType(uri);
            return value == null || value.isEmpty() ? fallback : value;
        } catch (RuntimeException ignored) {
            return fallback;
        }
    }

    @Nullable
    static String resourceDisplayName(Context context, Uri uri) {
        try (Cursor cursor = context.getContentResolver().query(
                 uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int column = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (column >= 0 && !cursor.isNull(column)) {
                    String value = cursor.getString(column);
                    if (value != null && !value.isEmpty())
                        return value;
                }
            }
        } catch (RuntimeException ignored) {
            // Providers are allowed to omit metadata or reject metadata queries.
        }
        String fallback = uri.getLastPathSegment();
        return fallback == null || fallback.isEmpty() ? null : fallback;
    }

    static int openUrl(ViewGroup parent, String url) {
        Uri uri = Uri.parse(url);
        if (uri.getScheme() == null || uri.getScheme().isEmpty())
            return -2;
        Intent intent = new Intent(Intent.ACTION_VIEW, uri);
        if (intent.resolveActivity(parent.getContext().getPackageManager()) == null)
            return -4;
        try {
            parent.getContext().startActivity(intent);
            return 0;
        } catch (RuntimeException error) {
            return -1;
        }
    }

    static int openResource(ViewGroup parent, String uriValue, @Nullable String mimeType) {
        Uri uri = Uri.parse(uriValue);
        if (uri.getScheme() == null || uri.getScheme().isEmpty() ||
            "file".equalsIgnoreCase(uri.getScheme()))
            return -2;
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(uri, mimeType);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        if (intent.resolveActivity(parent.getContext().getPackageManager()) == null)
            return -4;
        try {
            parent.getContext().startActivity(intent);
            return 0;
        } catch (RuntimeException error) {
            return -1;
        }
    }

    static int openResourceFd(ViewGroup parent, String uriValue, int flags) {
        Uri uri = Uri.parse(uriValue);
        if (uri.getScheme() == null || uri.getScheme().isEmpty())
            return -1;
        boolean read = (flags & 1) != 0;
        boolean write = (flags & 2) != 0;
        boolean truncate = (flags & 8) != 0;
        String mode = read && write ? (truncate ? "rwt" : "rw")
                                   : write ? (truncate ? "wt" : "rw")
                                           : "r";
        try {
            ParcelFileDescriptor descriptor = parent.getContext().getContentResolver()
                                                  .openFileDescriptor(uri, mode);
            return descriptor == null ? -1 : descriptor.detachFd();
        } catch (RuntimeException | java.io.FileNotFoundException error) {
            return -1;
        }
    }

    static int persistedResourceAccess(ViewGroup parent, String uriValue) {
        Uri uri = Uri.parse(uriValue);
        if (uri.getScheme() == null || !"content".equalsIgnoreCase(uri.getScheme()))
            return -2;
        try {
            for (UriPermission permission :
                 parent.getContext().getContentResolver().getPersistedUriPermissions()) {
                if (!uri.equals(permission.getUri()))
                    continue;
                int flags = permission.isReadPermission() ? 1 : 0;
                if (permission.isWritePermission())
                    flags |= 2;
                return flags == 0 ? 0 : flags | 4;
            }
            return 0;
        } catch (RuntimeException error) {
            return -1;
        }
    }

    static int setPersistedResourceAccess(ViewGroup parent, String uriValue, int desired) {
        int current = persistedResourceAccess(parent, uriValue);
        if (current < 0)
            return current;
        Uri uri = Uri.parse(uriValue);
        int currentAccess = current & 3;
        try {
            int acquire = desired & ~currentAccess;
            if (acquire != 0)
                parent.getContext().getContentResolver().takePersistableUriPermission(
                    uri, androidGrantFlags(acquire));
            current = persistedResourceAccess(parent, uriValue);
            if (current < 0)
                return current;
            int release = (current & 3) & ~desired;
            if (release != 0)
                parent.getContext().getContentResolver().releasePersistableUriPermission(
                    uri, androidGrantFlags(release));
            return persistedResourceAccess(parent, uriValue);
        } catch (SecurityException error) {
            return -4;
        } catch (RuntimeException error) {
            return -1;
        }
    }

    private static int androidGrantFlags(int access) {
        int flags = (access & 1) != 0 ? Intent.FLAG_GRANT_READ_URI_PERMISSION : 0;
        if ((access & 2) != 0)
            flags |= Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        return flags;
    }

    static int share(ViewGroup parent, @Nullable String title, @Nullable String text,
                     String[] uriValues, String[] mimeTypes, String[] displayNames) {
        ArrayList<Uri> uris = new ArrayList<>();
        for (String value : uriValues) {
            Uri uri = Uri.parse(value);
            if (uri.getScheme() == null || uri.getScheme().isEmpty() ||
                "file".equalsIgnoreCase(uri.getScheme()))
                return -2;
            uris.add(uri);
        }
        Intent intent = new Intent(uris.size() > 1 ? Intent.ACTION_SEND_MULTIPLE
                                                   : Intent.ACTION_SEND);
        if (text != null)
            intent.putExtra(Intent.EXTRA_TEXT, text);
        String type = commonMimeType(mimeTypes);
        intent.setType(type);
        if (uris.size() == 1)
            intent.putExtra(Intent.EXTRA_STREAM, uris.get(0));
        else if (!uris.isEmpty())
            intent.putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris);
        if (!uris.isEmpty()) {
            ClipData clip = new ClipData(displayNames.length == 0 ? "NativeKit"
                                                                  : displayNames[0],
                                             new String[] {type},
                                             new ClipData.Item(uris.get(0)));
            for (int index = 1; index < uris.size(); ++index)
                clip.addItem(new ClipData.Item(uris.get(index)));
            intent.setClipData(clip);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        }
        try {
            parent.getContext().startActivity(Intent.createChooser(intent, title));
            return 0;
        } catch (RuntimeException error) {
            return -1;
        }
    }

    private static String commonMimeType(String[] mimeTypes) {
        String result = null;
        for (String value : mimeTypes) {
            if (value == null || value.isEmpty())
                return "*/*";
            if (result == null)
                result = value;
            else if (!result.equals(value))
                return "*/*";
        }
        return result == null ? "text/plain" : result;
    }

    static boolean setClipboardText(ViewGroup parent, String text) {
        ClipboardManager clipboard = (ClipboardManager)parent.getContext().getSystemService(
            Context.CLIPBOARD_SERVICE);
        if (clipboard == null)
            return false;
        clipboard.setPrimaryClip(ClipData.newPlainText("NativeKit", text));
        return true;
    }

    static boolean setClipboardResources(ViewGroup parent, String[] uriValues, String[] mimeTypes,
                                         String[] displayNames) {
        ClipboardManager clipboard = (ClipboardManager)parent.getContext().getSystemService(
            Context.CLIPBOARD_SERVICE);
        if (clipboard == null || uriValues.length == 0)
            return false;
        Uri first = Uri.parse(uriValues[0]);
        if (first.getScheme() == null || "file".equalsIgnoreCase(first.getScheme()))
            return false;
        ArrayList<String> clipTypes = new ArrayList<>();
        clipTypes.add("text/uri-list");
        for (String mimeType : mimeTypes) {
            if (mimeType != null && !mimeType.isEmpty() && !clipTypes.contains(mimeType))
                clipTypes.add(mimeType);
        }
        ClipData clip = new ClipData(displayNames.length == 0 ? "NativeKit" : displayNames[0],
                                     clipTypes.toArray(new String[0]), new ClipData.Item(first));
        for (int index = 1; index < uriValues.length; ++index) {
            Uri uri = Uri.parse(uriValues[index]);
            if (uri.getScheme() == null || "file".equalsIgnoreCase(uri.getScheme()))
                return false;
            clip.addItem(new ClipData.Item(uri));
        }
        clipboard.setPrimaryClip(clip);
        return true;
    }

    @Nullable
    static String[] clipboardResources(ViewGroup parent) {
        ClipboardManager clipboard = (ClipboardManager)parent.getContext().getSystemService(
            Context.CLIPBOARD_SERVICE);
        if (clipboard == null || !clipboard.hasPrimaryClip())
            return null;
        ClipData clip = clipboard.getPrimaryClip();
        if (clip == null)
            return null;
        String fallbackMimeType = null;
        for (int index = 0; index < clip.getDescription().getMimeTypeCount(); ++index) {
            String candidate = clip.getDescription().getMimeType(index);
            if (!"text/uri-list".equals(candidate)) {
                fallbackMimeType = candidate;
                break;
            }
        }
        ArrayList<String> resources = new ArrayList<>();
        for (int index = 0; index < clip.getItemCount(); ++index) {
            Uri uri = clip.getItemAt(index).getUri();
            if (uri != null) {
                resources.add(uri.toString());
                resources.add(resourceMimeType(parent.getContext(), uri, fallbackMimeType));
                resources.add(resourceDisplayName(parent.getContext(), uri));
            }
        }
        return resources.toArray(new String[0]);
    }

    @Nullable
    static String clipboardText(ViewGroup parent) {
        ClipboardManager clipboard = (ClipboardManager)parent.getContext().getSystemService(
            Context.CLIPBOARD_SERVICE);
        if (clipboard == null || !clipboard.hasPrimaryClip())
            return null;
        ClipData clip = clipboard.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0)
            return null;
        CharSequence text = clip.getItemAt(0).coerceToText(parent.getContext());
        return text == null ? null : text.toString();
    }

    static boolean startFileDialog(ViewGroup parent, long request, int kind, int flags,
                                   @Nullable String title, @Nullable String suggestedName,
                                   @Nullable String[] patterns) {
        Context context = parent.getContext();
        Intent intent = new Intent(context, NativeKitDialogActivity.class);
        intent.putExtra(NativeKitDialogActivity.EXTRA_REQUEST, request);
        intent.putExtra(NativeKitDialogActivity.EXTRA_KIND, kind);
        intent.putExtra(NativeKitDialogActivity.EXTRA_FLAGS, flags);
        intent.putExtra(NativeKitDialogActivity.EXTRA_TITLE, title);
        intent.putExtra(NativeKitDialogActivity.EXTRA_SUGGESTED_NAME, suggestedName);
        intent.putExtra(NativeKitDialogActivity.EXTRA_PATTERNS, patterns);
        try {
            context.startActivity(intent);
            return true;
        } catch (RuntimeException error) {
            return false;
        }
    }

    static void cancelFileDialog(long request) {
        if (!NativeKitDialogActivity.cancel(request))
            cancelledDialogs.add(request);
    }

    static boolean takeDialogCancellation(long request) {
        return cancelledDialogs.remove(request);
    }

    static boolean showNotification(ViewGroup parent, long request, int flags, String title,
                                    String body, int timeoutMs) {
        Intent intent = new Intent(parent.getContext(), NativeKitNotificationActivity.class);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_REQUEST, request);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_FLAGS, flags);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_TITLE, title);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_BODY, body);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_TIMEOUT, timeoutMs);
        try {
            parent.getContext().startActivity(intent);
            return true;
        } catch (RuntimeException error) {
            return false;
        }
    }

    static void closeNotification(ViewGroup parent, long request) {
        NativeKitNotificationActivity.cancel(parent.getContext(), request);
    }

    @Nullable
    static String systemDirectory(ViewGroup parent, int kind) {
        Context context = parent.getContext();
        switch (kind) {
            case 1:
            case 6:
            case 7:
                return context.getFilesDir().getAbsolutePath();
            case 3:
                return externalDirectory(context, Environment.DIRECTORY_DOCUMENTS);
            case 4:
                return externalDirectory(context, Environment.DIRECTORY_DOWNLOADS);
            case 5:
            case 8:
                return context.getCacheDir().getAbsolutePath();
            default:
                return null;
        }
    }

    private static String externalDirectory(Context context, String kind) {
        java.io.File directory = context.getExternalFilesDir(kind);
        return directory == null ? context.getFilesDir().getAbsolutePath()
                                 : directory.getAbsolutePath();
    }

    static String systemLocale(ViewGroup parent) {
        Configuration configuration = parent.getResources().getConfiguration();
        Locale locale = Build.VERSION.SDK_INT >= 24 && !configuration.getLocales().isEmpty()
            ? configuration.getLocales().get(0)
            : configuration.locale;
        return locale.toLanguageTag();
    }

    static int systemAppearance(ViewGroup parent) {
        int night = parent.getResources().getConfiguration().uiMode &
            Configuration.UI_MODE_NIGHT_MASK;
        int scheme = night == Configuration.UI_MODE_NIGHT_YES ? 2
                     : night == Configuration.UI_MODE_NIGHT_NO ? 1
                                                               : 0;
        boolean highContrast = Settings.Secure.getInt(parent.getContext().getContentResolver(),
                                                      "high_text_contrast_enabled", 0) != 0;
        return scheme | (highContrast ? 0x100 : 0);
    }

    static void observeHost(ViewGroup parent, long handle) {
        observedHosts.put(handle, parent);
        observeInputDevices(parent.getContext());
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
        setDropEnabled(parent, handle, false);
        if (observedHosts.isEmpty() && inputManager != null && inputDeviceListener != null) {
            for (int deviceId : InputDevice.getDeviceIds())
                if (gamepadDevice(InputDevice.getDevice(deviceId)))
                    nativeOnGamepadDisconnected(deviceId);
            inputManager.unregisterInputDeviceListener(inputDeviceListener);
            inputManager = null;
            inputDeviceListener = null;
        }
    }

    private static boolean gamepadDevice(InputDevice device) {
        if (device == null)
            return false;
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
               (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static void registerGamepad(@Nullable InputDevice device, int fallbackId) {
        if (device == null) {
            nativeOnGamepadConnected(fallbackId, "Android game controller",
                                     "android-controller-" + fallbackId);
        } else if (gamepadDevice(device)) {
            nativeOnGamepadConnected(device.getId(), device.getName(), device.getDescriptor());
        }
    }

    private static void observeInputDevices(Context context) {
        if (inputManager != null)
            return;
        inputManager = (InputManager)context.getSystemService(Context.INPUT_SERVICE);
        if (inputManager == null)
            return;
        inputDeviceListener = new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) {
                registerGamepad(InputDevice.getDevice(deviceId), deviceId);
            }

            @Override
            public void onInputDeviceChanged(int deviceId) {
                registerGamepad(InputDevice.getDevice(deviceId), deviceId);
            }

            @Override
            public void onInputDeviceRemoved(int deviceId) {
                nativeOnGamepadDisconnected(deviceId);
            }
        };
        inputManager.registerInputDeviceListener(inputDeviceListener, null);
        for (int deviceId : InputDevice.getDeviceIds())
            registerGamepad(InputDevice.getDevice(deviceId), deviceId);
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

    private static @Modifiers int modifiers(int meta) {
        int result = 0;
        if ((meta & KeyEvent.META_SHIFT_ON) != 0) result |= MOD_SHIFT;
        if ((meta & KeyEvent.META_CTRL_ON) != 0) result |= MOD_CONTROL;
        if ((meta & KeyEvent.META_ALT_ON) != 0) result |= MOD_ALT;
        if ((meta & KeyEvent.META_META_ON) != 0) result |= MOD_SUPER;
        if ((meta & KeyEvent.META_CAPS_LOCK_ON) != 0) result |= MOD_CAPS_LOCK;
        if ((meta & KeyEvent.META_NUM_LOCK_ON) != 0) result |= MOD_NUM_LOCK;
        return result;
    }

    private static @PointerButton int pointerButton(int button) {
        if (button == MotionEvent.BUTTON_SECONDARY) return POINTER_BUTTON_RIGHT;
        if (button == MotionEvent.BUTTON_TERTIARY) return POINTER_BUTTON_MIDDLE;
        if (button == MotionEvent.BUTTON_BACK) return POINTER_BUTTON_4;
        if (button == MotionEvent.BUTTON_FORWARD) return POINTER_BUTTON_5;
        return POINTER_BUTTON_LEFT;
    }

    private static int nativeKey(int key) {
        if (key >= KeyEvent.KEYCODE_A && key <= KeyEvent.KEYCODE_Z)
            return KEY_A + key - KeyEvent.KEYCODE_A;
        if (key >= KeyEvent.KEYCODE_0 && key <= KeyEvent.KEYCODE_9)
            return KEY_0 + key - KeyEvent.KEYCODE_0;
        if (key >= KeyEvent.KEYCODE_F1 && key <= KeyEvent.KEYCODE_F12)
            return KEY_F1 + key - KeyEvent.KEYCODE_F1;
        switch (key) {
            case KeyEvent.KEYCODE_SPACE: return KEY_SPACE;
            case KeyEvent.KEYCODE_APOSTROPHE: return KEY_APOSTROPHE;
            case KeyEvent.KEYCODE_COMMA: return KEY_COMMA;
            case KeyEvent.KEYCODE_MINUS: return KEY_MINUS;
            case KeyEvent.KEYCODE_PERIOD: return KEY_PERIOD;
            case KeyEvent.KEYCODE_SLASH: return KEY_SLASH;
            case KeyEvent.KEYCODE_SEMICOLON: return KEY_SEMICOLON;
            case KeyEvent.KEYCODE_EQUALS: return KEY_EQUAL;
            case KeyEvent.KEYCODE_LEFT_BRACKET: return KEY_LEFT_BRACKET;
            case KeyEvent.KEYCODE_BACKSLASH: return KEY_BACKSLASH;
            case KeyEvent.KEYCODE_RIGHT_BRACKET: return KEY_RIGHT_BRACKET;
            case KeyEvent.KEYCODE_GRAVE: return KEY_GRAVE_ACCENT;
            case KeyEvent.KEYCODE_ESCAPE: return KEY_ESCAPE;
            case KeyEvent.KEYCODE_ENTER: return KEY_ENTER;
            case KeyEvent.KEYCODE_TAB: return KEY_TAB;
            case KeyEvent.KEYCODE_DEL: return KEY_BACKSPACE;
            case KeyEvent.KEYCODE_INSERT: return KEY_INSERT;
            case KeyEvent.KEYCODE_FORWARD_DEL: return KEY_DELETE;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return KEY_RIGHT;
            case KeyEvent.KEYCODE_DPAD_LEFT: return KEY_LEFT;
            case KeyEvent.KEYCODE_DPAD_DOWN: return KEY_DOWN;
            case KeyEvent.KEYCODE_DPAD_UP: return KEY_UP;
            case KeyEvent.KEYCODE_PAGE_UP: return KEY_PAGE_UP;
            case KeyEvent.KEYCODE_PAGE_DOWN: return KEY_PAGE_DOWN;
            case KeyEvent.KEYCODE_MOVE_HOME: return KEY_HOME;
            case KeyEvent.KEYCODE_MOVE_END: return KEY_END;
            case KeyEvent.KEYCODE_CAPS_LOCK: return KEY_CAPS_LOCK;
            case KeyEvent.KEYCODE_SCROLL_LOCK: return KEY_SCROLL_LOCK;
            case KeyEvent.KEYCODE_NUM_LOCK: return KEY_NUM_LOCK;
            case KeyEvent.KEYCODE_SYSRQ: return KEY_PRINT_SCREEN;
            case KeyEvent.KEYCODE_BREAK: return KEY_PAUSE;
            case KeyEvent.KEYCODE_SHIFT_LEFT: return KEY_LEFT_SHIFT;
            case KeyEvent.KEYCODE_CTRL_LEFT: return KEY_LEFT_CONTROL;
            case KeyEvent.KEYCODE_ALT_LEFT: return KEY_LEFT_ALT;
            case KeyEvent.KEYCODE_META_LEFT: return KEY_LEFT_SUPER;
            case KeyEvent.KEYCODE_SHIFT_RIGHT: return KEY_RIGHT_SHIFT;
            case KeyEvent.KEYCODE_CTRL_RIGHT: return KEY_RIGHT_CONTROL;
            case KeyEvent.KEYCODE_ALT_RIGHT: return KEY_RIGHT_ALT;
            case KeyEvent.KEYCODE_META_RIGHT: return KEY_RIGHT_SUPER;
            case KeyEvent.KEYCODE_MENU: return KEY_MENU;
            default: return KEY_UNKNOWN;
        }
    }

    private static int gamepadButton(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_BUTTON_A: return GAMEPAD_BUTTON_A;
            case KeyEvent.KEYCODE_BUTTON_B: return GAMEPAD_BUTTON_B;
            case KeyEvent.KEYCODE_BUTTON_X: return GAMEPAD_BUTTON_X;
            case KeyEvent.KEYCODE_BUTTON_Y: return GAMEPAD_BUTTON_Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return GAMEPAD_BUTTON_LEFT_BUMPER;
            case KeyEvent.KEYCODE_BUTTON_R1: return GAMEPAD_BUTTON_RIGHT_BUMPER;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return GAMEPAD_BUTTON_BACK;
            case KeyEvent.KEYCODE_BUTTON_START: return GAMEPAD_BUTTON_START;
            case KeyEvent.KEYCODE_BUTTON_MODE: return GAMEPAD_BUTTON_GUIDE;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return GAMEPAD_BUTTON_LEFT_THUMB;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return GAMEPAD_BUTTON_RIGHT_THUMB;
            case KeyEvent.KEYCODE_DPAD_UP: return GAMEPAD_BUTTON_DPAD_UP;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return GAMEPAD_BUTTON_DPAD_RIGHT;
            case KeyEvent.KEYCODE_DPAD_DOWN: return GAMEPAD_BUTTON_DPAD_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return GAMEPAD_BUTTON_DPAD_LEFT;
            default: return -1;
        }
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
    private static native void nativeOnSurfaceCreated(long handle, Surface surface);
    private static native void nativeOnSurfaceChanged(long handle, int width, int height,
                                                      int framebufferWidth,
                                                      int framebufferHeight);
    private static native void nativeOnSurfaceDestroyed(long handle);
    private static native void nativeOnTouch(long handle, int pointerId, int action, int tool,
                                             float x, float y, float pressure, float tiltX,
                                             float tiltY, int modifiers);
    private static native void nativeOnPointerMove(long handle, float x, float y);
    private static native void nativeOnPointerEnter(long handle, boolean entered);
    private static native void nativeOnPointerButton(long handle, int button, boolean pressed,
                                                     int modifiers, float x, float y);
    private static native void nativeOnPointerScroll(long handle, float x, float y);
    private static native void nativeOnKey(long handle, int key, int scanCode, int action,
                                          int modifiers);
    private static native void nativeOnText(long handle, int codepoint);
    private static native void nativeOnGamepadAxis(long handle, int device, int axis, float value);
    private static native void nativeOnGamepadButton(long handle, int device, int button,
                                                     boolean pressed);
    private static native void nativeOnGamepadConnected(int device, String name,
                                                        String descriptor);
    private static native void nativeOnGamepadDisconnected(int device);
    private static native void nativeOnGeometry(long handle, int width, int height, float scale,
                                                int insetLeft, int insetTop, int insetRight,
                                                int insetBottom, int keyboardBottom);
    static native void nativeOnFileDialog(long request, int kind, boolean accepted,
                                          @Nullable String[] uris, @Nullable String[] mimeTypes,
                                          @Nullable String[] displayNames,
                                          @Nullable int[] resourceFlags);
    static native void nativeOnIncomingIntent(long host, int kind, @Nullable String text,
                                              @Nullable String subject, String[] uris,
                                              String[] mimeTypes, String[] displayNames,
                                              int[] resourceFlags);
    private static native void nativeOnResourceDrop(long host, float x, float y,
                                                    @Nullable String text, String[] uris,
                                                    String[] mimeTypes, String[] displayNames,
                                                    int[] resourceFlags);
    static native void nativeOnNotificationDelivered(long request);
    static native void nativeOnNotificationFailed(long request, String message);
    static native void nativeOnNotificationActivated(long request);
    static native void nativeOnNotificationDismissed(long request);
}
