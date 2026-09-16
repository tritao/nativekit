package io.nativekit;

/** Exact UTF-16/code-point conversions used at the Android IME boundary. */
final class NativeKitTextInputOffsets {
    static final int INVALID = -1;

    private NativeKitTextInputOffsets() {}

    /** Returns the code-point offset for an exact UTF-16 boundary, or INVALID. */
    static int codePointOffset(CharSequence text, int utf16Offset) {
        if (text == null || utf16Offset < 0 || utf16Offset > text.length())
            return INVALID;
        int codePoints = 0;
        for (int index = 0; index < utf16Offset;) {
            char value = text.charAt(index);
            if (Character.isHighSurrogate(value)) {
                if (index + 1 >= text.length() || !Character.isLowSurrogate(text.charAt(index + 1)) ||
                    index + 1 >= utf16Offset)
                    return INVALID;
                index += 2;
            } else if (Character.isLowSurrogate(value)) {
                return INVALID;
            } else {
                ++index;
            }
            ++codePoints;
        }
        return codePoints;
    }

    /** Returns the UTF-16 offset for a code-point offset, or INVALID. */
    static int utf16Offset(CharSequence text, int codePointOffset) {
        if (text == null || codePointOffset < 0)
            return INVALID;
        int index = 0;
        for (int codePoints = 0; codePoints < codePointOffset; ++codePoints) {
            if (index >= text.length())
                return INVALID;
            char value = text.charAt(index);
            if (Character.isHighSurrogate(value)) {
                if (index + 1 >= text.length() ||
                    !Character.isLowSurrogate(text.charAt(index + 1)))
                    return INVALID;
                index += 2;
            } else if (Character.isLowSurrogate(value)) {
                return INVALID;
            } else {
                ++index;
            }
        }
        return index;
    }

    /** Returns false for malformed UTF-16 or an offset inside a surrogate pair. */
    static boolean isExactBoundary(CharSequence text, int utf16Offset) {
        return codePointOffset(text, utf16Offset) != INVALID;
    }

    /** Moves a UTF-16 offset to the previous scalar boundary. */
    static int previousBoundary(CharSequence text, int utf16Offset) {
        if (text == null || utf16Offset < 0 || utf16Offset > text.length())
            return INVALID;
        if (utf16Offset < text.length() && Character.isLowSurrogate(text.charAt(utf16Offset))) {
            if (utf16Offset == 0 || !Character.isHighSurrogate(text.charAt(utf16Offset - 1)))
                return INVALID;
            return utf16Offset - 1;
        }
        return utf16Offset;
    }

    /** Moves a UTF-16 offset to the next scalar boundary. */
    static int nextBoundary(CharSequence text, int utf16Offset) {
        if (text == null || utf16Offset < 0 || utf16Offset > text.length())
            return INVALID;
        if (utf16Offset < text.length() && Character.isLowSurrogate(text.charAt(utf16Offset))) {
            if (utf16Offset == 0 || !Character.isHighSurrogate(text.charAt(utf16Offset - 1)))
                return INVALID;
            return utf16Offset + 1;
        }
        if (utf16Offset < text.length() &&
            Character.isHighSurrogate(text.charAt(utf16Offset)) &&
            (utf16Offset + 1 >= text.length() ||
             !Character.isLowSurrogate(text.charAt(utf16Offset + 1))))
            return INVALID;
        return utf16Offset;
    }

    /**
     * Calculates a scalar-safe deletion range for Android's UTF-16 lengths.
     *
     * Android keyboards normally report a complete surrogate pair for an emoji,
     * but expanding a partial request keeps the editor from ever splitting one.
     */
    static int[] deletionRange(CharSequence text, int selectionStart, int selectionEnd,
                               int beforeLength, int afterLength) {
        if (text == null || beforeLength < 0 || afterLength < 0 ||
            !isExactBoundary(text, selectionStart) || !isExactBoundary(text, selectionEnd))
            return null;
        int first = Math.min(selectionStart, selectionEnd);
        int last = Math.max(selectionStart, selectionEnd);
        first = beforeLength > first ? 0 : first - beforeLength;
        last = afterLength > text.length() - last ? text.length() : last + afterLength;
        first = previousBoundary(text, first);
        last = nextBoundary(text, last);
        if (first == INVALID || last == INVALID || first > last)
            return null;
        return new int[] {first, last};
    }

    /** Calculates a deletion range when the platform reports code-point lengths. */
    static int[] deletionRangeInCodePoints(CharSequence text, int selectionStart, int selectionEnd,
                                           int beforeLength, int afterLength) {
        if (text == null || beforeLength < 0 || afterLength < 0)
            return null;
        int first = Math.min(selectionStart, selectionEnd);
        int last = Math.max(selectionStart, selectionEnd);
        int firstCodePoint = codePointOffset(text, first);
        int lastCodePoint = codePointOffset(text, last);
        int totalCodePoints = codePointOffset(text, text.length());
        if (firstCodePoint == INVALID || lastCodePoint == INVALID ||
            totalCodePoints == INVALID)
            return null;
        firstCodePoint = beforeLength > firstCodePoint
            ? 0 : firstCodePoint - beforeLength;
        lastCodePoint = afterLength > totalCodePoints - lastCodePoint
            ? totalCodePoints : lastCodePoint + afterLength;
        int firstUtf16 = utf16Offset(text, firstCodePoint);
        int lastUtf16 = utf16Offset(text, lastCodePoint);
        if (firstUtf16 == INVALID || lastUtf16 == INVALID)
            return null;
        return new int[] {firstUtf16, lastUtf16};
    }
}
