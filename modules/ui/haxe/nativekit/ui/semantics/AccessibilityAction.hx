package nativekit.ui.semantics;

/** Action capability bits shared with NativeKit's accessibility backend. */
class AccessibilityAction {
	public static inline var Activate = 1;
	public static inline var Focus = 2;
	public static inline var SetValue = 4;
	public static inline var SetSelection = 8;
	public static inline var Increment = 16;
	public static inline var Decrement = 32;
	public static inline var ScrollForward = 64;
	public static inline var ScrollBackward = 128;
	public static inline var MoveNext = 256;
	public static inline var MovePrevious = 512;
}
