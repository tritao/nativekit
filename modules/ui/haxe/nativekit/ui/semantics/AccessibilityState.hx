package nativekit.ui.semantics;

/** State bits shared with NativeKit's accessibility backend. */
class AccessibilityState {
	public static inline var Focusable = 1;
	public static inline var Focused = 2;
	public static inline var Selected = 4;
	public static inline var Checked = 8;
	public static inline var Disabled = 16;
	public static inline var ReadOnly = 32;
	public static inline var Multiline = 64;
	public static inline var Password = 128;
	public static inline var Expanded = 256;
}
