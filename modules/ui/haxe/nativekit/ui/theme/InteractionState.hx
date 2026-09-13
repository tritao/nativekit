package nativekit.ui.theme;

/** Compact persistent interaction flags used while resolving view styles. */
class InteractionState {
	public static inline var Hovered:Int = 1 << 0;
	public static inline var Pressed:Int = 1 << 1;
	public static inline var Focused:Int = 1 << 2;
	public static inline var Selected:Int = 1 << 3;

	public static inline function contains(flags:Int, flag:Int):Bool
		return (flags & flag) != 0;

	public static inline function with(flags:Int, flag:Int, enabled:Bool):Int
		return enabled ? flags | flag : flags & ~flag;
}
