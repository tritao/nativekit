package io.nativekit.consumer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ConsumerTest {
    @Test
    public void projectDependencyLinksAndCallsNativeKit() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                assertEquals(1, activity.apiVersion());
                assertNotEquals(0, activity.webViewHandle());
                assertEquals(0, activity.resourceClipboardProbe());
            });
        }
    }
}
