package io.nativekit.sample;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.pm.ActivityInfo;
import android.Manifest;
import android.app.LocaleManager;
import android.app.UiModeManager;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Build;
import android.os.LocaleList;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import io.nativekit.NativeKitEvent;
import java.net.URLEncoder;
import java.io.File;
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
    private static final int EVENT_WEBVIEW_PROCESS_TERMINATED = 205;
    private static final int EVENT_CLIPBOARD_TEXT_COMPLETE = 400;
    private static final int EVENT_DIALOG_COMPLETE = 101;
    private static final int EVENT_NOTIFICATION_DELIVERED = 500;
    private static final int EVENT_NOTIFICATION_ACTIVATED = 501;
    private static final int EVENT_NOTIFICATION_DISMISSED = 502;
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

            long[] crashedWebView = new long[1];
            scenario.onActivity(activity -> crashedWebView[0] =
                                    activity.host.createWebView(16, 16, "chrome://crash"));
            NativeKitEvent terminated =
                awaitEventForSource(scenario, EVENT_WEBVIEW_PROCESS_TERMINATED, crashedWebView[0]);
            assertEquals(1, terminated.flags);

            long[] recoveredWebView = new long[1];
            scenario.onActivity(activity -> recoveredWebView[0] = activity.host.createWebView(
                                    16, 16, "data:text/html,<title>recovered</title>"));
            NativeKitEvent recovered =
                awaitEventForSource(scenario, EVENT_WEBVIEW_NAVIGATED, recoveredWebView[0]);
            assertEquals(recoveredWebView[0], recovered.source);

            long[] clipboardRequest = new long[1];
            scenario.onActivity(activity -> {
                assertEquals(0, activity.host.setClipboardText(message));
                clipboardRequest[0] = activity.host.readClipboardText();
            });
            assertNotEquals(0, clipboardRequest[0]);
            NativeKitEvent clipboard = awaitEvent(scenario, EVENT_CLIPBOARD_TEXT_COMPLETE);
            assertEquals(clipboardRequest[0], clipboard.requestId);
            assertEquals(message, clipboard.text());

            assertDialogCanStartAndCancel(scenario, 1);
            assertDialogCanStartAndCancel(scenario, 2);
            assertDialogCanStartAndCancel(scenario, 3);

            if (Build.VERSION.SDK_INT >= 33) {
                InstrumentationRegistry.getInstrumentation().getUiAutomation()
                    .grantRuntimePermission("io.nativekit.sample",
                                            Manifest.permission.POST_NOTIFICATIONS);
            }
            long[] notificationRequest = new long[1];
            scenario.onActivity(activity -> notificationRequest[0] =
                                    activity.host.showNotification("NativeKit", message));
            assertNotEquals(0, notificationRequest[0]);
            NativeKitEvent delivered = awaitEvent(scenario, EVENT_NOTIFICATION_DELIVERED);
            assertEquals(notificationRequest[0], delivered.requestId);
            sendNotificationAction(scenario, "io.nativekit.NOTIFICATION_ACTIVATE",
                                   notificationRequest[0]);
            NativeKitEvent activated = awaitEvent(scenario, EVENT_NOTIFICATION_ACTIVATED);
            assertEquals(notificationRequest[0], activated.requestId);
            sendNotificationAction(scenario, "io.nativekit.NOTIFICATION_DISMISS",
                                   notificationRequest[0]);
            NativeKitEvent userDismissed = awaitEvent(scenario, EVENT_NOTIFICATION_DISMISSED);
            assertEquals(notificationRequest[0], userDismissed.requestId);

            scenario.onActivity(activity -> notificationRequest[0] =
                                    activity.host.showNotification("NativeKit close", message));
            awaitEvent(scenario, EVENT_NOTIFICATION_DELIVERED);
            scenario.onActivity(activity ->
                assertEquals(0, activity.host.closeNotification(notificationRequest[0])));
            NativeKitEvent dismissed = awaitEvent(scenario, EVENT_NOTIFICATION_DISMISSED);
            assertEquals(notificationRequest[0], dismissed.requestId);

            scenario.onActivity(activity -> {
                int[] directories = {1, 3, 4, 5, 6, 7, 8};
                for (int kind : directories) {
                    String path = activity.host.systemDirectory(kind);
                    assertNotNull(path);
                    assertTrue(new File(path).isAbsolute());
                }
                assertEquals(null, activity.host.systemDirectory(2));
                assertTrue(!activity.host.systemLocale().isEmpty());
                int scheme = activity.host.systemAppearance() & 0xff;
                assertTrue(scheme >= 0 && scheme <= 2);
            });
            if (Build.VERSION.SDK_INT >= 31) {
                scenario.onActivity(activity ->
                    activity.getSystemService(UiModeManager.class).setApplicationNightMode(
                        UiModeManager.MODE_NIGHT_YES));
                awaitColorScheme(scenario, 2);
                scenario.onActivity(activity ->
                    activity.getSystemService(UiModeManager.class).setApplicationNightMode(
                        UiModeManager.MODE_NIGHT_NO));
                awaitColorScheme(scenario, 1);
            }
            if (Build.VERSION.SDK_INT >= 33) {
                scenario.onActivity(activity -> activity.getSystemService(LocaleManager.class)
                                                    .setApplicationLocales(
                                                        LocaleList.forLanguageTags("pt-PT")));
                awaitLocale(scenario, "pt-PT");
                scenario.onActivity(activity -> activity.getSystemService(LocaleManager.class)
                                                    .setApplicationLocales(
                                                        LocaleList.getEmptyLocaleList()));
            }

            scenario.onActivity(activity -> {
                assertEquals(-2, activity.host.openUrl("not a URL"));
                assertEquals(0, activity.host.openUrl("nativekit-test://opened"));
            });
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

    private static void assertDialogCanStartAndCancel(
        ActivityScenario<NativeKitTestActivity> scenario, int kind) throws Exception {
        long[] request = new long[1];
        scenario.onActivity(activity -> {
            if (kind == 1)
                request[0] = activity.host.openFileDialog("Choose a document");
            else if (kind == 2)
                request[0] = activity.host.saveFileDialog("Save a document", "nativekit.txt");
            else
                request[0] = activity.host.selectDirectoryDialog("Choose a directory");
            assertNotEquals(0, request[0]);
            assertEquals(0, activity.host.cancelDialog(request[0]));
        });
        NativeKitEvent dialog = awaitEvent(scenario, EVENT_DIALOG_COMPLETE);
        assertEquals(request[0], dialog.requestId);
        assertEquals(kind + 4, dialog.flags);
        assertNotNull(dialog.data);
        assertEquals(0, geometryData(dialog).getInt(0));
        assertEquals(0, geometryData(dialog).getInt(4));
    }

    private static void sendNotificationAction(ActivityScenario<NativeKitTestActivity> scenario,
                                               String action, long request) {
        scenario.onActivity(activity -> {
            Intent intent = new Intent(action);
            intent.setComponent(
                new ComponentName(activity, "io.nativekit.NativeKitNotificationReceiver"));
            intent.putExtra("nativekit.notification.request", request);
            activity.sendBroadcast(intent);
        });
    }

    private static void awaitColorScheme(ActivityScenario<NativeKitTestActivity> scenario,
                                         int expected) throws Exception {
        awaitCondition(scenario, activity -> (activity.host.systemAppearance() & 0xff) == expected,
                       "color scheme " + expected);
    }

    private static void awaitLocale(ActivityScenario<NativeKitTestActivity> scenario,
                                    String expected) throws Exception {
        awaitCondition(scenario, activity -> activity.host.systemLocale().equals(expected),
                       "locale " + expected);
    }

    private interface ActivityCondition { boolean test(NativeKitTestActivity activity); }

    private static void awaitCondition(ActivityScenario<NativeKitTestActivity> scenario,
                                       ActivityCondition condition, String description)
        throws Exception {
        long deadline = System.currentTimeMillis() + 10_000;
        while (System.currentTimeMillis() < deadline) {
            boolean[] matched = {false};
            scenario.onActivity(activity -> matched[0] = condition.test(activity));
            if (matched[0])
                return;
            Thread.sleep(50);
        }
        throw new AssertionError("timed out waiting for " + description);
    }

    private static NativeKitEvent awaitEventForSource(
        ActivityScenario<NativeKitTestActivity> scenario, int kind, long source) throws Exception {
        long deadline = System.currentTimeMillis() + 10_000;
        while (System.currentTimeMillis() < deadline) {
            List<NativeKitEvent> events = new ArrayList<>();
            scenario.onActivity(activity -> {
                NativeKitEvent event;
                while ((event = activity.host.pollEvent()) != null)
                    events.add(event);
            });
            for (NativeKitEvent event : events)
                if (event.kind == kind && event.source == source)
                    return event;
            Thread.sleep(50);
        }
        throw new AssertionError("timed out waiting for event " + kind + " from " + source);
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
