package io.nativekit.consumer;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileNotFoundException;

/** Private provider used to exercise NativeKit's content-URI stream bridge. */
public final class TestResourceProvider extends ContentProvider {
    @Override
    public boolean onCreate() { return true; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode)
        throws FileNotFoundException {
        File directory = new File(getContext().getCacheDir(), "resource-provider");
        if (!directory.isDirectory() && !directory.mkdirs())
            throw new FileNotFoundException("could not create provider directory");
        return ParcelFileDescriptor.open(new File(directory, "probe.bin"),
                                         ParcelFileDescriptor.parseMode(mode));
    }

    @Override
    public String getType(Uri uri) { return "application/octet-stream"; }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
                        String sortOrder) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) { return null; }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
