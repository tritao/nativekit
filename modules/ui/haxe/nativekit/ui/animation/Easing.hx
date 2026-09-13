package nativekit.ui.animation;

/** Numeric easing curves evaluated by Haxe before each render transaction. */
class Easing {
	public static inline var Linear:Int = 0;
	public static inline var EaseIn:Int = 1;
	public static inline var EaseOut:Int = 2;
	public static inline var EaseInOut:Int = 3;

	public static function apply(value:Float, curve:Int):Float {
		var t = value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
		return switch curve {
			case EaseIn: t * t;
			case EaseOut: 1.0 - (1.0 - t) * (1.0 - t);
			case EaseInOut: t * t * (3.0 - 2.0 * t);
			case Linear: t;
			default: throw "Unknown easing curve";
		};
	}
}
