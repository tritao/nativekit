package io.nativekit.consumer;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
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
                assertEquals(1, activity.apiVersion());
                assertNotEquals(0, activity.webViewHandle());
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
}
