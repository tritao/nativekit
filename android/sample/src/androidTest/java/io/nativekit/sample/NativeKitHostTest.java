package io.nativekit.sample;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.pm.ActivityInfo;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import io.nativekit.NativeKitEvent;
import java.net.URLEncoder;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class NativeKitHostTest {
    private static final int EVENT_WEBVIEW_NAVIGATED = 200;
    private static final int EVENT_WEBVIEW_MESSAGE = 201;
    private static final int EVENT_WEBVIEW_NAVIGATION_FAILED = 204;
    private static final int EVENT_HOST_GEOMETRY_CHANGED = 600;

    @Test
    public void hostWebViewLifecycleAndEventsWorkEndToEnd() throws Exception {
        try (ActivityScenario<NativeKitTestActivity> scenario =
                 ActivityScenario.launch(NativeKitTestActivity.class)) {
            long[] handles = new long[2];
            String message = "Olá, 世界 🌍";
            String html = "<meta name=viewport content='width=device-width'>"
                + "<script>setTimeout(()=>window.webkit.messageHandlers.nativekit."
                + "postMessage('" + message + "'),50)</script><p>ready</p>";
            String url = "data:text/html;charset=utf-8,"
                + URLEncoder.encode(html, StandardCharsets.UTF_8).replace("+", "%20");

            scenario.onActivity(activity -> {
                handles[0] = activity.host.handle();
                handles[1] = activity.host.createWebView(320, 240, url);
                activity.host.inactive();
                activity.host.background();
                activity.host.active();
            });
            assertNotEquals(0, handles[0]);
            assertNotEquals(0, handles[1]);

            List<NativeKitEvent> events = awaitEvents(scenario, EVENT_WEBVIEW_NAVIGATED,
                                                       EVENT_WEBVIEW_MESSAGE,
                                                       EVENT_HOST_GEOMETRY_CHANGED);
            NativeKitEvent messageEvent = find(events, EVENT_WEBVIEW_MESSAGE);
            assertEquals("\"" + message + "\"", messageEvent.text());

            NativeKitEvent geometry = events.stream()
                                          .filter(event -> event.kind == EVENT_HOST_GEOMETRY_CHANGED)
                                          .filter(event -> event.data != null)
                                          .filter(event -> geometryData(event).getInt(4) > 0)
                                          .filter(event -> geometryData(event).getInt(8) > 0)
                                          .reduce((first, last) -> last)
                                          .orElseThrow();
            assertEquals(handles[0], geometry.source);
            assertNotNull(geometry.data);
            assertTrue(geometry.data.length >= 36);
            ByteBuffer initialGeometry = geometryData(geometry);
            assertTrue(initialGeometry.getInt(4) > 0);
            assertTrue(initialGeometry.getInt(8) > 0);
            assertTrue(initialGeometry.getFloat(12) > 0);
            for (int offset = 16; offset <= 32; offset += 4)
                assertTrue(initialGeometry.getInt(offset) >= 0);

            scenario.onActivity(activity -> activity.setRequestedOrientation(
                                    ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
            awaitLandscapeGeometry(scenario);

            scenario.onActivity(activity ->
                activity.host.createWebView(16, 16, "file:///definitely-not-present-nativekit"));
            NativeKitEvent failure = awaitEvent(scenario, EVENT_WEBVIEW_NAVIGATION_FAILED);
            assertTrue(failure.flags >= 0 && failure.flags <= 6);
            assertNotNull(failure.text());
        }
    }

    private static List<NativeKitEvent> awaitEvents(
        ActivityScenario<NativeKitTestActivity> scenario, int... kinds) throws Exception {
        List<NativeKitEvent> events = new ArrayList<>();
        long deadline = System.currentTimeMillis() + 10_000;
        while (System.currentTimeMillis() < deadline) {
            scenario.onActivity(activity -> {
                NativeKitEvent event;
                while ((event = activity.host.pollEvent()) != null)
                    events.add(event);
            });
            boolean complete = true;
            for (int kind : kinds)
                complete &= events.stream().anyMatch(event -> event.kind == kind);
            if (complete)
                return events;
            Thread.sleep(50);
        }
        throw new AssertionError("timed out waiting for NativeKit events: " + events.size());
    }

    private static NativeKitEvent awaitEvent(ActivityScenario<NativeKitTestActivity> scenario,
                                              int kind) throws Exception {
        return find(awaitEvents(scenario, kind), kind);
    }

    private static void awaitLandscapeGeometry(ActivityScenario<NativeKitTestActivity> scenario)
        throws Exception {
        long deadline = System.currentTimeMillis() + 10_000;
        while (System.currentTimeMillis() < deadline) {
            boolean[] landscape = {false};
            scenario.onActivity(activity -> {
                NativeKitEvent event;
                while ((event = activity.host.pollEvent()) != null) {
                    if (event.kind == EVENT_HOST_GEOMETRY_CHANGED && event.data != null) {
                        ByteBuffer geometry = geometryData(event);
                        landscape[0] |= geometry.getInt(4) > geometry.getInt(8);
                    }
                }
            });
            if (landscape[0])
                return;
            Thread.sleep(50);
        }
        throw new AssertionError("timed out waiting for landscape geometry");
    }

    private static ByteBuffer geometryData(NativeKitEvent event) {
        return ByteBuffer.wrap(event.data).order(ByteOrder.nativeOrder());
    }

    private static NativeKitEvent find(List<NativeKitEvent> events, int kind) {
        return events.stream().filter(event -> event.kind == kind).findFirst().orElseThrow();
    }
}
