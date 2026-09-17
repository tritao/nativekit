package io.nativekit;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public final class NativeKitTextInputOffsetsTest {
    @Test
    public void mapsAstralCodePointsOnlyAtExactUtf16Boundaries() {
        String text = "A😀B";

        assertEquals(0, NativeKitTextInputOffsets.codePointOffset(text, 0));
        assertEquals(1, NativeKitTextInputOffsets.codePointOffset(text, 1));
        assertEquals(NativeKitTextInputOffsets.INVALID,
                     NativeKitTextInputOffsets.codePointOffset(text, 2));
        assertEquals(2, NativeKitTextInputOffsets.codePointOffset(text, 3));
        assertEquals(3, NativeKitTextInputOffsets.codePointOffset(text, 4));
        assertEquals(3, NativeKitTextInputOffsets.utf16Offset(text, 2));
        assertEquals(NativeKitTextInputOffsets.INVALID,
                     NativeKitTextInputOffsets.utf16Offset("😀", 2));
    }

    @Test
    public void rejectsMalformedUtf16BeforeMappedPosition() {
        String high = new String(new char[] {'a', '\ud800'});
        String low = new String(new char[] {'a', '\udc00'});

        assertFalse(NativeKitTextInputOffsets.isExactBoundary(high, high.length()));
        assertFalse(NativeKitTextInputOffsets.isExactBoundary(low, low.length()));
        assertEquals(NativeKitTextInputOffsets.INVALID,
                     NativeKitTextInputOffsets.codePointOffset("😀", 1));
    }

    @Test
    public void deletionExpandsPartialSurrogateRequests() {
        String text = "a😀b";

        assertArrayEquals(new int[] {1, 3}, NativeKitTextInputOffsets.deletionRange(
            text, 3, 3, 1, 0));
        assertArrayEquals(new int[] {1, 3}, NativeKitTextInputOffsets.deletionRange(
            text, 1, 1, 0, 1));
        assertArrayEquals(new int[] {1, 4}, NativeKitTextInputOffsets.deletionRange(
            text, 3, 3, 2, 1));
        assertArrayEquals(new int[] {1, 3}, NativeKitTextInputOffsets.deletionRangeInCodePoints(
            text, 3, 3, 1, 0));
        assertNull(NativeKitTextInputOffsets.deletionRange(text, 2, 2, 1, 0));
    }

    @Test
    public void surroundingRangeKeepsSelectionAndSurrogatesIntact() {
        String text = "a😀b";

        assertArrayEquals(new int[] {1, 4}, NativeKitTextInputOffsets.surroundingRange(
            text, 3, 3, 2, 1));
        assertArrayEquals(new int[] {0, 4}, NativeKitTextInputOffsets.surroundingRange(
            text, 1, 3, 1, 1));
        assertNull(NativeKitTextInputOffsets.surroundingRange(text, 2, 2, 1, 0));
    }
}
