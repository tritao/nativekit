package io.nativekit.consumer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import android.accessibilityservice.AccessibilityServiceInfo;
import android.app.UiAutomation;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ConsumerTest {
    @Test
    public void projectDependencyLinksAndCallsNativeKit() {
        Intent launch = new Intent(Intent.ACTION_SEND);
        launch.setClass(androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                            .getTargetContext(), MainActivity.class);
        launch.setType("text/plain");
        launch.putExtra(Intent.EXTRA_TEXT, "shared text");
        launch.putExtra(Intent.EXTRA_SUBJECT, "shared subject");
        Uri first = Uri.parse("content://io.nativekit.consumer.resources/one");
        Uri second = Uri.parse("content://io.nativekit.consumer.resources/two");
        launch.putExtra(Intent.EXTRA_STREAM, first);
        launch.setClipData(new ClipData("resources", new String[] {"text/plain"},
                                        new ClipData.Item(first)));
        launch.getClipData().addItem(new ClipData.Item(second));
        launch.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(launch)) {
            scenario.onActivity(activity -> {
                assertEquals(2, activity.apiVersion());
                assertNotEquals(0, activity.webViewHandle());
                assertEquals(0, activity.webViewHistoryProbe());
                assertEquals(0, activity.incomingShareProbe());
                assertEquals(0, activity.resourceClipboardProbe());
                assertEquals(0, activity.resourceStreamProbe());
                assertEquals(0, activity.persistedResourceProbe());
                Intent view = new Intent(Intent.ACTION_VIEW);
                view.setDataAndType(
                    Uri.parse("content://io.nativekit.consumer.resources/viewed"),
                    "application/octet-stream");
                view.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                assertEquals(0, activity.dispatchIntentForTest(view));
                assertEquals(0, activity.incomingViewProbe());
            });
        }
    }

    @Test
    public void graphicsSurfacesRenderAndRecoverAcrossLifecycle() throws Exception {
        Intent launch = new Intent(
            androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                .getTargetContext(), MainActivity.class);
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(launch)) {
            scenario.onActivity(MainActivity::createGraphicsSurfaceProbe);
            waitForIdle();
            scenario.onActivity(activity -> {
                assertNotEquals(0, activity.graphicsSurfaceHandle());
                assertEquals("initial graphics surface probe", 0,
                             activity.graphicsSurfaceProbe());
                activity.dispatchInputForTest();
                assertEquals("graphics surface input probe", 0, activity.inputProbe());
            });
            UiAutomation automation =
                androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                    .getUiAutomation();
            AccessibilityServiceInfo service = automation.getServiceInfo();
            service.flags |= AccessibilityServiceInfo.FLAG_REQUEST_TOUCH_EXPLORATION_MODE;
            automation.setServiceInfo(service);
            waitForAccessibilityReady(scenario);
            executeAndWaitForAccessibilityEvent(
                automation, () -> scenario.onActivity(MainActivity::dispatchAccessibilityForTest),
                AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED, "window content changed");
            executeAndWaitForAccessibilityEvent(
                automation,
                () -> scenario.onActivity(MainActivity::dispatchAccessibilityHoverForTest),
                AccessibilityEvent.TYPE_VIEW_HOVER_ENTER, "view hover enter");
            scenario.onActivity(activity -> {
                assertEquals("graphics surface accessibility probe", 0,
                             activity.accessibilityProbe());
                assertEquals("hide graphics surface", 0,
                             activity.setGraphicsSurfaceVisible(false));
            });
            waitForIdle();
            scenario.onActivity(activity -> {
                assertEquals("graphics surface lost event", 0,
                             activity.graphicsSurfaceLifecycleProbe(702));
                assertEquals("show graphics surface", 0,
                             activity.setGraphicsSurfaceVisible(true));
            });
            waitForIdle();
            scenario.onActivity(activity ->
                assertEquals("graphics surface restored after visibility", 0,
                             activity.graphicsSurfaceLifecycleProbe(700)));

            scenario.moveToState(androidx.lifecycle.Lifecycle.State.CREATED);
            waitForIdle();
            scenario.moveToState(androidx.lifecycle.Lifecycle.State.RESUMED);
            waitForIdle();
            scenario.onActivity(activity ->
                assertEquals("graphics surface restored after activity resume", 0,
                             activity.graphicsSurfaceLifecycleProbe(700)));

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                scenario.onActivity(MainActivity::createVulkanSurfaceProbe);
                waitForIdle();
                scenario.onActivity(activity -> {
                    assertNotEquals(0, activity.vulkanSurfaceHandle());
                    assertEquals("Vulkan surface probe", 0, activity.vulkanSurfaceProbe());
                    assertEquals("hide Vulkan surface", 0,
                                 activity.setVulkanSurfaceVisible(false));
                });
                waitForIdle();
                scenario.onActivity(activity -> {
                    assertEquals("Vulkan surface lost event", 0,
                                 activity.vulkanSurfaceLostProbe());
                    assertEquals("show Vulkan surface", 0,
                                 activity.setVulkanSurfaceVisible(true));
                });
                waitForIdle();
                scenario.onActivity(activity ->
                    assertEquals("Vulkan surface recreated", 0,
                                 activity.vulkanSurfaceRecreatedProbe()));
            }

            scenario.recreate();
            waitForIdle();
            scenario.onActivity(MainActivity::createGraphicsSurfaceProbe);
            waitForIdle();
            scenario.onActivity(activity -> {
                assertNotEquals(0, activity.graphicsSurfaceHandle());
                assertEquals("graphics surface after activity recreation", 0,
                             activity.graphicsSurfaceProbe());
            });
        }
    }

    private static void waitForIdle() {
        androidx.test.platform.app.InstrumentationRegistry.getInstrumentation().waitForIdleSync();
    }

    private static void waitForAccessibilityReady(ActivityScenario<MainActivity> scenario)
        throws InterruptedException {
        AtomicBoolean ready = new AtomicBoolean();
        long deadline = SystemClock.uptimeMillis() + 5_000;
        do {
            scenario.onActivity(activity -> {
                AccessibilityManager manager =
                    activity.getSystemService(AccessibilityManager.class);
                ready.set(manager != null && manager.isEnabled() &&
                          manager.isTouchExplorationEnabled());
            });
            if (ready.get()) {
                waitForIdle();
                return;
            }
            Thread.sleep(50);
        } while (SystemClock.uptimeMillis() < deadline);
        throw new AssertionError("Android accessibility service did not become ready");
    }

    private static void executeAndWaitForAccessibilityEvent(UiAutomation automation,
                                                             Runnable action, int eventType,
                                                             String description) throws Exception {
        try {
            automation.executeAndWaitForEvent(action,
                event -> event.getEventType() == eventType, 10_000);
        } catch (TimeoutException error) {
            throw new AssertionError("timed out waiting for accessibility event " + description,
                                     error);
        }
    }
}
