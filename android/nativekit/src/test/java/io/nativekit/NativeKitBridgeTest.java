package io.nativekit;

import static org.junit.Assert.assertEquals;

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
}
