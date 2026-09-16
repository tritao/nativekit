package nativekit.ui.widgets;

/**
 * An offset between Unicode scalar values in a NativeKit document.
 *
 * This is the coordinate system used by Skribidi and the editor transaction
 * protocol. It is deliberately distinct from UTF-8 bytes and UTF-16 units.
 */
enum abstract CodepointOffset(Int) from Int to Int {}
