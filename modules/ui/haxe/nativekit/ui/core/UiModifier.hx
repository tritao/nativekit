package nativekit.ui.core;

/** Modifier bit values shared with NativeKit input events. */
class UiModifier {
	public static inline var Shift:Int = 1 << 0;
	public static inline var Control:Int = 1 << 1;
	public static inline var Alt:Int = 1 << 2;
	public static inline var Super:Int = 1 << 3;
	public static inline var CapsLock:Int = 1 << 4;
	public static inline var NumLock:Int = 1 << 5;
}
