package nativekit.ui.widgets;

/**
 * A code-point offset that has been aligned to a user-perceived grapheme
 * boundary. Grapheme positions use document coordinates, not cluster ordinals.
 */
enum abstract GraphemePosition(Int) from Int to Int {}
