package nativekit.ui.style;

/**
 * Rendering-independent description of one post-layout visual effect.
 *
 * Concrete effects own typed parameters and value behavior. They do not know
 * how those parameters become render passes, shaders, or intermediate targets.
 */
class Effect {
	public final kind:EffectKind;

	function new(kind:EffectKind) {
		this.kind = kind;
	}

	public function isEqual(other:Effect):Bool
		return other != null && kind == other.kind;

	/** Interpolates compatible effects and uses a discrete midpoint otherwise. */
	public function interpolate(other:Effect, amount:Float):Effect {
		if (other == null || kind != other.kind)
			return discrete(this, other, amount);
		return amount < 0.5 ? copy() : other.copy();
	}

	public static function discrete(left:Effect, right:Effect, amount:Float):Effect
		return amount < 0.5 ? left.copy() : right == null ? left.copy() : right.copy();

	public function copy():Effect
		return new Effect(kind);

	public function inkOverflow():InkOverflow
		return InkOverflow.zero();

	public function describe():String
		return Std.string(kind);

	public function toString():String
		return describe();

	public static function requireFinite(value:Float, message:String):Float {
		if (!Math.isFinite(value))
			throw message;
		return value;
	}

	public static function requireNonNegative(value:Float, message:String):Float {
		if (!Math.isFinite(value) || value < 0.0)
			throw message;
		return value;
	}

	public static function interpolateFloat(left:Float, right:Float, amount:Float):Float
		return left + (right - left) * amount;

	public static function copyColor(value:Color):Color
		return value == null ? null : Color.rgba(value.red, value.green, value.blue, value.alpha);

	public static function equalColor(left:Color, right:Color):Bool
		return left == right || (left != null && right != null && left.red == right.red &&
			left.green == right.green && left.blue == right.blue && left.alpha == right.alpha);

	public static function interpolateColor(left:Color, right:Color, amount:Float):Color
		return Color.rgba(interpolateFloat(left.red, right.red, amount),
			interpolateFloat(left.green, right.green, amount),
			interpolateFloat(left.blue, right.blue, amount),
			interpolateFloat(left.alpha, right.alpha, amount));
}
