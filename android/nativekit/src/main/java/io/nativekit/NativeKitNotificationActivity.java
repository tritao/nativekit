package io.nativekit;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import androidx.annotation.Nullable;
import java.lang.ref.WeakReference;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** Internal proxy that requests notification permission and submits notifications. */
public final class NativeKitNotificationActivity extends Activity {
    static final String EXTRA_REQUEST = "nativekit.notification.request";
    static final String EXTRA_FLAGS = "nativekit.notification.flags";
    static final String EXTRA_TITLE = "nativekit.notification.title";
    static final String EXTRA_BODY = "nativekit.notification.body";
    static final String EXTRA_TIMEOUT = "nativekit.notification.timeout";
    private static final String CHANNEL = "nativekit.default";
    private static final int PERMISSION = 1;
    private static final Map<Long, WeakReference<NativeKitNotificationActivity>> active =
        new HashMap<>();
    private static final Set<Long> cancelled = new HashSet<>();
    private static final Set<Long> posted = new HashSet<>();
    private long request;

    @Override
    protected void onCreate(@Nullable Bundle state) {
        super.onCreate(state);
        request = getIntent().getLongExtra(EXTRA_REQUEST, 0);
        if (request == 0 || cancelled.remove(request)) {
            finish();
            return;
        }
        active.put(request, new WeakReference<>(this));
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) !=
                PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, PERMISSION);
        } else {
            submit();
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        if (code != PERMISSION)
            return;
        if (results.length != 0 && results[0] == PackageManager.PERMISSION_GRANTED)
            submit();
        else {
            active.remove(request);
            NativeKitBridge.nativeOnNotificationFailed(request,
                                                        "notification permission was denied");
            finish();
        }
    }

    private void submit() {
        NotificationManager manager =
            (NotificationManager)getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null) {
            active.remove(request);
            NativeKitBridge.nativeOnNotificationFailed(request,
                                                        "notification service is unavailable");
            finish();
            return;
        }
        if (Build.VERSION.SDK_INT >= 26) {
            manager.createNotificationChannel(new NotificationChannel(
                CHANNEL, "NativeKit", NotificationManager.IMPORTANCE_DEFAULT));
        }
        Intent activated = NativeKitNotificationReceiver.intent(this, request, true);
        Intent dismissed = NativeKitNotificationReceiver.intent(this, request, false);
        int pendingFlags = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
            ? new Notification.Builder(this, CHANNEL)
            : new Notification.Builder(this);
        int icon = getApplicationInfo().icon;
        builder.setSmallIcon(icon != 0 ? icon : android.R.drawable.stat_notify_more)
            .setContentTitle(getIntent().getStringExtra(EXTRA_TITLE))
            .setContentText(getIntent().getStringExtra(EXTRA_BODY))
            .setAutoCancel(true)
            .setContentIntent(PendingIntent.getBroadcast(this, notificationId(request), activated,
                                                         pendingFlags))
            .setDeleteIntent(PendingIntent.getBroadcast(this, notificationId(request) ^ 0x40000000,
                                                        dismissed, pendingFlags));
        int timeout = getIntent().getIntExtra(EXTRA_TIMEOUT, 0);
        if (Build.VERSION.SDK_INT >= 26 && timeout > 0)
            builder.setTimeoutAfter(timeout);
        if ((getIntent().getIntExtra(EXTRA_FLAGS, 0) & 1) != 0)
            builder.setDefaults(0).setSound(null).setVibrate(null);
        try {
            manager.notify(notificationId(request), builder.build());
            active.remove(request);
            posted.add(request);
            NativeKitBridge.nativeOnNotificationDelivered(request);
        } catch (RuntimeException error) {
            active.remove(request);
            NativeKitBridge.nativeOnNotificationFailed(request, error.toString());
        }
        finish();
    }

    static int notificationId(long request) { return (int)(request ^ (request >>> 32)); }

    static void cancel(Context context, long request) {
        WeakReference<NativeKitNotificationActivity> reference = active.remove(request);
        NativeKitNotificationActivity activity = reference == null ? null : reference.get();
        if (activity != null)
            activity.finish();
        else if (!posted.remove(request))
            cancelled.add(request);
        NotificationManager manager =
            (NotificationManager)context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null)
            manager.cancel(notificationId(request));
    }

    static void forget(long request) { posted.remove(request); }
}
