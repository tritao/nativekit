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
	public static inline var Toggle = 1024;
	public static inline var Select = 2048;
	public static inline var Deselect = 4096;
	public static inline var Expand = 8192;
	public static inline var Collapse = 16384;
	public static inline var Dismiss = 32768;
	public static inline var ShowContextMenu = 65536;
	public static inline var ScrollIntoView = 131072;
}
