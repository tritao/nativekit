package io.nativekit;

import static org.junit.Assert.assertEquals;

import android.view.inputmethod.EditorInfo;
import android.webkit.WebViewClient;
import org.junit.Test;

public final class NativeKitBridgeTest {
    @Test
    public void navigationErrorsUseStableNativeKitCategories() {
        assertEquals(2, NativeKitBridge.navigationErrorCategory(WebViewClient.ERROR_AUTHENTICATION));
        assertEquals(5, NativeKitBridge.navigationErrorCategory(WebViewClient.ERROR_TIMEOUT));
        assertEquals(4, NativeKitBridge.navigationErrorCategory(WebViewClient.ERROR_FILE_NOT_FOUND));
        assertEquals(3,
                     NativeKitBridge.navigationErrorCategory(
                         WebViewClient.ERROR_FAILED_SSL_HANDSHAKE));
        assertEquals(1, NativeKitBridge.navigationErrorCategory(WebViewClient.ERROR_BAD_URL));
        assertEquals(0, NativeKitBridge.navigationErrorCategory(WebViewClient.ERROR_UNKNOWN));
        assertEquals(0, NativeKitBridge.navigationErrorCategory(Integer.MIN_VALUE));
    }

    @Test
    public void editorActionsUseStableNativeKitCategories() {
        assertEquals(0, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_UNSPECIFIED));
        assertEquals(1, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_DONE));
        assertEquals(2, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_GO));
        assertEquals(3, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_NEXT));
        assertEquals(4, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_SEARCH));
        assertEquals(5, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_SEND));
        assertEquals(6, NativeKitBridge.nativeTextInputAction(EditorInfo.IME_ACTION_NONE));
        assertEquals(-1, NativeKitBridge.nativeTextInputAction(Integer.MIN_VALUE));
    }
}
