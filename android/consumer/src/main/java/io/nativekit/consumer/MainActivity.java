package io.nativekit.consumer;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.text.InputType;
import android.widget.FrameLayout;
import io.nativekit.NativeKitHost;

/** Minimal consumer: Java owns lifecycle while native code calls the NativeKit C ABI. */
public final class MainActivity extends Activity {
    static { System.loadLibrary("nativekit_consumer"); }

    private NativeKitHost nativeKit;
    private long probe;
    private long surfaceProbe;
    private long vulkanSurfaceProbe;
    private FrameLayout content;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        content = new FrameLayout(this);
        setContentView(content);
        nativeKit = new NativeKitHost(content);
        nativeKit.dispatchIntent(getIntent());
        probe = nativeProbe(nativeKit.handle());
    }

    public int apiVersion() { return (int)(probe >>> 32); }

    public long webViewHandle() { return probe & 0xffffffffL; }

    public int resourceClipboardProbe() { return nativeResourceClipboardProbe(); }

    public int resourceStreamProbe() { return nativeResourceStreamProbe(); }

    public int webViewHistoryProbe() { return nativeWebViewHistoryProbe(webViewHandle()); }

    public long graphicsSurfaceHandle() { return surfaceProbe; }

    public int graphicsSurfaceProbe() { return nativeGraphicsSurfaceProbe(surfaceProbe); }

    public int inputProbe() { return nativeInputProbe(surfaceProbe); }

    public void dispatchInputForTest() {
        SurfaceView view = null;
        for (int index = 0; index < content.getChildCount(); ++index) {
            if (content.getChildAt(index) instanceof SurfaceView) {
                view = (SurfaceView)content.getChildAt(index);
                break;
            }
        }
        if (view == null)
            throw new IllegalStateException("graphics surface is unavailable");
        long now = SystemClock.uptimeMillis();
        MotionEvent.PointerProperties[] properties = {
            pointer(7, MotionEvent.TOOL_TYPE_FINGER), pointer(11, MotionEvent.TOOL_TYPE_FINGER)};
        MotionEvent.PointerCoords[] coordinates = {coordinates(12, 18, 0.6f),
                                                    coordinates(32, 38, 0.8f)};
        dispatchTouch(view, now, MotionEvent.ACTION_DOWN, 1, properties, coordinates);
        dispatchTouch(view, now, MotionEvent.ACTION_POINTER_DOWN |
                                     (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                      2, properties, coordinates);
        dispatchTouch(view, now, MotionEvent.ACTION_MOVE, 2, properties, coordinates);
        dispatchTouch(view, now, MotionEvent.ACTION_POINTER_UP |
                                     (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                      2, properties, coordinates);
        dispatchTouch(view, now, MotionEvent.ACTION_UP, 1, properties, coordinates);

        MotionEvent.PointerProperties[] stylus = {pointer(19, MotionEvent.TOOL_TYPE_STYLUS)};
        MotionEvent.PointerCoords stylusCoordinates = coordinates(24, 28, 0.7f);
        stylusCoordinates.setAxisValue(MotionEvent.AXIS_TILT, 0.4f);
        MotionEvent.PointerCoords[] stylusValues = {stylusCoordinates};
        dispatchTouch(view, now, MotionEvent.ACTION_DOWN, 1, stylus, stylusValues);
        dispatchTouch(view, now, MotionEvent.ACTION_UP, 1, stylus, stylusValues);

        MotionEvent.PointerCoords mouseCoordinates = coordinates(40, 44, 0f);
        mouseCoordinates.setAxisValue(MotionEvent.AXIS_HSCROLL, 1.5f);
        mouseCoordinates.setAxisValue(MotionEvent.AXIS_VSCROLL, -2f);
        MotionEvent.PointerProperties[] mouse = {pointer(0, MotionEvent.TOOL_TYPE_MOUSE)};
        MotionEvent.PointerCoords[] mouseValues = {mouseCoordinates};
        dispatchGeneric(view, now, MotionEvent.ACTION_HOVER_ENTER, InputDevice.SOURCE_MOUSE,
                        0, mouse, mouseValues);
        dispatchGeneric(view, now, MotionEvent.ACTION_SCROLL, InputDevice.SOURCE_MOUSE,
                        0, mouse, mouseValues);
        MotionEvent button = generic(now, MotionEvent.ACTION_BUTTON_PRESS,
                                     InputDevice.SOURCE_MOUSE, 0, MotionEvent.BUTTON_SECONDARY,
                                     mouse, mouseValues);
        view.dispatchGenericMotionEvent(button);
        button.recycle();

        KeyEvent keyDown = new KeyEvent(now, now, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_A, 0,
                                        KeyEvent.META_SHIFT_ON, 3, 30, 0,
                                        InputDevice.SOURCE_KEYBOARD);
        KeyEvent keyUp = KeyEvent.changeAction(keyDown, KeyEvent.ACTION_UP);
        view.dispatchKeyEvent(keyDown);
        view.dispatchKeyEvent(keyUp);

        nativePrepareTextInput(surfaceProbe);
        EditorInfo editorInfo = new EditorInfo();
        InputConnection editor = view.onCreateInputConnection(editorInfo);
        if ((editorInfo.inputType & InputType.TYPE_MASK_VARIATION) !=
                InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS ||
            (editorInfo.inputType & InputType.TYPE_TEXT_FLAG_AUTO_CORRECT) == 0 ||
            (editorInfo.inputType & InputType.TYPE_TEXT_FLAG_CAP_SENTENCES) == 0 ||
            (editorInfo.imeOptions & EditorInfo.IME_MASK_ACTION) != EditorInfo.IME_ACTION_SEND ||
            editorInfo.initialSelStart != 8 || editorInfo.initialSelEnd != 8 ||
            !"hello \ud83d\ude00".contentEquals(editor.getTextBeforeCursor(32, 0)))
            throw new AssertionError("text input snapshot was not exposed to the IME");
        editor.beginBatchEdit();
        editor.setComposingText("に", 1);
        editor.setComposingText("日本", 1);
        editor.commitText("日本語", 1);
        editor.endBatchEdit();
        editor.setSelection(5, 5);
        editor.deleteSurroundingText(1, 0);
        editor.setComposingRegion(0, 2);
        editor.finishComposingText();

        MotionEvent.PointerCoords gamepadCoordinates = coordinates(0, 0, 0f);
        gamepadCoordinates.setAxisValue(MotionEvent.AXIS_X, 0.5f);
        gamepadCoordinates.setAxisValue(MotionEvent.AXIS_Y, -0.25f);
        gamepadCoordinates.setAxisValue(MotionEvent.AXIS_LTRIGGER, 0.75f);
        dispatchGeneric(view, now, MotionEvent.ACTION_MOVE, InputDevice.SOURCE_JOYSTICK,
                        42, new MotionEvent.PointerProperties[] {pointer(0, 0)},
                        new MotionEvent.PointerCoords[] {gamepadCoordinates});
        KeyEvent gamepad = new KeyEvent(now, now, KeyEvent.ACTION_DOWN,
                                        KeyEvent.KEYCODE_BUTTON_A, 0, 0, 42, 0, 0,
                                        InputDevice.SOURCE_GAMEPAD);
        view.dispatchKeyEvent(gamepad);
    }

    private static MotionEvent.PointerProperties pointer(int id, int tool) {
        MotionEvent.PointerProperties value = new MotionEvent.PointerProperties();
        value.id = id;
        value.toolType = tool;
        return value;
    }

    private static MotionEvent.PointerCoords coordinates(float x, float y, float pressure) {
        MotionEvent.PointerCoords value = new MotionEvent.PointerCoords();
        value.x = x;
        value.y = y;
        value.pressure = pressure;
        value.size = 1;
        return value;
    }

    private static MotionEvent generic(long time, int action, int source, int device,
                                       int buttonState,
                                       MotionEvent.PointerProperties[] properties,
                                       MotionEvent.PointerCoords[] coordinates) {
        return MotionEvent.obtain(time, time, action, 1, properties, coordinates, 0, buttonState,
                                  1, 1, device, 0, source, 0);
    }

    private static void dispatchGeneric(SurfaceView view, long time, int action, int source,
                                        int device, MotionEvent.PointerProperties[] properties,
                                        MotionEvent.PointerCoords[] coordinates) {
        MotionEvent event = generic(time, action, source, device, 0, properties, coordinates);
        view.dispatchGenericMotionEvent(event);
        event.recycle();
    }

    private static void dispatchTouch(SurfaceView view, long time, int action, int count,
                                      MotionEvent.PointerProperties[] properties,
                                      MotionEvent.PointerCoords[] coordinates) {
        MotionEvent event = MotionEvent.obtain(time, time, action, count, properties, coordinates,
                                               0, 0, 1, 1, 0, 0,
                                               InputDevice.SOURCE_TOUCHSCREEN, 0);
        view.dispatchTouchEvent(event);
        event.recycle();
    }

    public void createGraphicsSurfaceProbe() {
        surfaceProbe = nativeCreateSurfaceProbe(nativeKit.handle());
    }

    public int setGraphicsSurfaceVisible(boolean visible) {
        return nativeSetSurfaceVisible(surfaceProbe, visible);
    }

    public int graphicsSurfaceLifecycleProbe(int eventKind) {
        return nativeSurfaceLifecycleProbe(surfaceProbe, eventKind);
    }

    public void createVulkanSurfaceProbe() {
        vulkanSurfaceProbe = nativeCreateVulkanSurfaceProbe(nativeKit.handle());
    }

    public long vulkanSurfaceHandle() { return vulkanSurfaceProbe; }

    public int vulkanSurfaceProbe() { return nativeVulkanSurfaceProbe(vulkanSurfaceProbe); }

    public int setVulkanSurfaceVisible(boolean visible) {
        return nativeSetSurfaceVisible(vulkanSurfaceProbe, visible);
    }

    public int vulkanSurfaceLostProbe() {
        return nativeVulkanSurfaceLostProbe(vulkanSurfaceProbe);
    }

    public int vulkanSurfaceRecreatedProbe() {
        return nativeVulkanSurfaceRecreatedProbe(vulkanSurfaceProbe);
    }

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
    private static native long nativeCreateSurfaceProbe(long host);
    private static native int nativeGraphicsSurfaceProbe(long surface);
    private static native int nativeInputProbe(long surface);
    private static native int nativeSetSurfaceVisible(long surface, boolean visible);
    private static native int nativeSurfaceLifecycleProbe(long surface, int eventKind);
    private static native long nativeCreateVulkanSurfaceProbe(long host);
    private static native int nativeVulkanSurfaceProbe(long surface);
    private static native int nativeVulkanSurfaceLostProbe(long surface);
    private static native int nativeVulkanSurfaceRecreatedProbe(long surface);
    private static native int nativeResourceClipboardProbe();
    private static native int nativePrepareTextInput(long surface);
    private static native int nativeResourceStreamProbe();
    private static native int nativeWebViewHistoryProbe(long webView);
    private static native int nativePersistedResourceProbe();
    private static native int nativeIncomingShareProbe();
    private static native int nativeIncomingViewProbe();
}
