package nativekit.ui.style;

/** Operations kept separate because Haxeon treats enum abstracts as constants. */
class StyleStateUtil {
	public static inline function contains(flags:Int, state:StyleState):Bool
		return (flags & state) != 0;

	public static inline function withState(flags:Int, state:StyleState, enabled:Bool):Int
		return enabled ? flags | state : flags & ~state;

	public static function count(flags:Int):Int {
		var result = 0;
		var remaining = flags;
		while (remaining != 0) {
			result += remaining & 1;
			remaining = remaining >>> 1;
		}
		return result;
	}
}
