package io.nativekit;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Internal receiver for notification activation and user dismissal. */
public final class NativeKitNotificationReceiver extends BroadcastReceiver {
    private static final String ACTION_ACTIVATE = "io.nativekit.NOTIFICATION_ACTIVATE";
    private static final String ACTION_DISMISS = "io.nativekit.NOTIFICATION_DISMISS";

    static Intent intent(Context context, long request, boolean activate) {
        Intent intent = new Intent(context, NativeKitNotificationReceiver.class);
        intent.setAction(activate ? ACTION_ACTIVATE : ACTION_DISMISS);
        intent.putExtra(NativeKitNotificationActivity.EXTRA_REQUEST, request);
        return intent;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        long request = intent.getLongExtra(NativeKitNotificationActivity.EXTRA_REQUEST, 0);
        if (request == 0)
            return;
        NativeKitNotificationActivity.forget(request);
        if (ACTION_ACTIVATE.equals(intent.getAction()))
            NativeKitBridge.nativeOnNotificationActivated(request);
        else if (ACTION_DISMISS.equals(intent.getAction()))
            NativeKitBridge.nativeOnNotificationDismissed(request);
    }
}
