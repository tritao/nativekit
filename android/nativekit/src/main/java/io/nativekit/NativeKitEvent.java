package io.nativekit;

import androidx.annotation.Nullable;
import java.nio.charset.StandardCharsets;

/** Immutable event returned by {@link NativeKitHost#pollEvent()}. */
public final class NativeKitEvent {
    public final int kind;
    public final long source;
    public final int flags;
    public final long requestId;
    public final int result;
    @Nullable public final byte[] data;

    NativeKitEvent(int kind, long source, int flags, long requestId, int result,
                   @Nullable byte[] data) {
        this.kind = kind;
        this.source = source;
        this.flags = flags;
        this.requestId = requestId;
        this.result = result;
        this.data = data;
    }

    /** Decodes a textual event payload as standard UTF-8. */
    @Nullable
    public String text() {
        return data == null ? null : new String(data, StandardCharsets.UTF_8);
    }
}
