package io.nativekit;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.webkit.MimeTypeMap;
import androidx.annotation.Nullable;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;

/** Internal proxy that owns Storage Access Framework activity results. */
public final class NativeKitDialogActivity extends Activity {
    static final String EXTRA_REQUEST = "nativekit.request";
    static final String EXTRA_KIND = "nativekit.kind";
    static final String EXTRA_FLAGS = "nativekit.flags";
    static final String EXTRA_TITLE = "nativekit.title";
    static final String EXTRA_SUGGESTED_NAME = "nativekit.suggestedName";
    static final String EXTRA_PATTERNS = "nativekit.patterns";
    private static final int PICK = 1;
    private static final Map<Long, WeakReference<NativeKitDialogActivity>> active = new HashMap<>();

    private long request;
    private int kind;

    @Override
    protected void onCreate(@Nullable Bundle state) {
        super.onCreate(state);
        request = getIntent().getLongExtra(EXTRA_REQUEST, 0);
        kind = getIntent().getIntExtra(EXTRA_KIND, 0);
        if (request == 0 || NativeKitBridge.takeDialogCancellation(request)) {
            finish();
            return;
        }
        active.put(request, new WeakReference<>(this));
        Intent picker = pickerIntent(getIntent());
        try {
            startActivityForResult(picker, PICK);
        } catch (RuntimeException error) {
            complete(false, null, null, null, null);
        }
    }

    private static Intent pickerIntent(Intent options) {
        int kind = options.getIntExtra(EXTRA_KIND, 0);
        String action = kind == 2 || kind == 6 ? Intent.ACTION_CREATE_DOCUMENT
                                  : kind == 3 || kind == 7 ? Intent.ACTION_OPEN_DOCUMENT_TREE
                                              : Intent.ACTION_OPEN_DOCUMENT;
        Intent picker = new Intent(action);
        if (kind != 3 && kind != 7)
            picker.addCategory(Intent.CATEGORY_OPENABLE);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE,
                        (options.getIntExtra(EXTRA_FLAGS, 0) & 1) != 0);
        picker.putExtra(Intent.EXTRA_TITLE, options.getStringExtra(EXTRA_TITLE));
        if (kind == 2 || kind == 6)
            picker.putExtra(Intent.EXTRA_TITLE, options.getStringExtra(EXTRA_SUGGESTED_NAME));
        if (kind != 3 && kind != 7) {
            String[] mimeTypes = mimeTypes(options.getStringArrayExtra(EXTRA_PATTERNS));
            picker.setType(mimeTypes.length == 1 ? mimeTypes[0] : "*/*");
            if (mimeTypes.length > 1)
                picker.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
        }
        return picker;
    }

    private static String[] mimeTypes(@Nullable String[] patterns) {
        Set<String> result = new LinkedHashSet<>();
        if (patterns != null) {
            for (String patternList : patterns) {
                if (patternList == null)
                    continue;
                for (String pattern : patternList.split(";")) {
                    int dot = pattern.lastIndexOf('.');
                    if (dot >= 0 && dot + 1 < pattern.length()) {
                        String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(
                            pattern.substring(dot + 1).replace("*", "").toLowerCase());
                        if (mime != null)
                            result.add(mime);
                    }
                }
            }
        }
        return result.toArray(new String[0]);
    }

    @Override
    protected void onActivityResult(int code, int resultCode, @Nullable Intent data) {
        super.onActivityResult(code, resultCode, data);
        if (code != PICK)
            return;
        ArrayList<String> uris = new ArrayList<>();
        ArrayList<String> mimeTypes = new ArrayList<>();
        ArrayList<String> displayNames = new ArrayList<>();
        ArrayList<Integer> resourceFlags = new ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            ClipData clips = data.getClipData();
            if (clips != null) {
                for (int index = 0; index < clips.getItemCount(); ++index) {
                    Uri uri = clips.getItemAt(index).getUri();
                    resourceFlags.add(resourceFlags(data, uri));
                    uris.add(uri.toString());
                    mimeTypes.add(NativeKitBridge.resourceMimeType(this, uri, data.getType()));
                    displayNames.add(NativeKitBridge.resourceDisplayName(this, uri));
                }
            } else {
                Uri uri = data.getData();
                if (uri != null) {
                    resourceFlags.add(resourceFlags(data, uri));
                    uris.add(uri.toString());
                    mimeTypes.add(NativeKitBridge.resourceMimeType(this, uri, data.getType()));
                    displayNames.add(NativeKitBridge.resourceDisplayName(this, uri));
                }
            }
        }
        int[] flags = new int[resourceFlags.size()];
        for (int index = 0; index < resourceFlags.size(); ++index)
            flags[index] = resourceFlags.get(index);
        complete(resultCode == RESULT_OK && !uris.isEmpty(), uris.toArray(new String[0]),
                 mimeTypes.toArray(new String[0]), displayNames.toArray(new String[0]), flags);
    }

    private void complete(boolean accepted, @Nullable String[] uris, @Nullable String[] mimeTypes,
                          @Nullable String[] displayNames, @Nullable int[] flags) {
        active.remove(request);
        NativeKitBridge.nativeOnFileDialog(request, kind, accepted, uris, mimeTypes, displayNames,
                                           flags);
        finish();
    }

    private int resourceFlags(Intent result, Uri uri) {
        int grants = result.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                          Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        int flags = (grants & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0 ? 1 : 0;
        if ((grants & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0)
            flags |= 2;
        try {
            getContentResolver().takePersistableUriPermission(uri, grants);
            flags |= 4;
        } catch (SecurityException ignored) {
            // Some document providers grant access without supporting persistable permissions.
        }
        return flags;
    }

    static boolean cancel(long request) {
        WeakReference<NativeKitDialogActivity> reference = active.remove(request);
        NativeKitDialogActivity activity = reference == null ? null : reference.get();
        if (activity != null) {
            activity.finishActivity(PICK);
            activity.finish();
            return true;
        }
        return false;
    }
}
