package nativekit.ui.style;

/**
 * Extra paint extent requested by a visual effect, in logical pixels.
 *
 * This is deliberately a value type. It describes the geometry contract
 * between style and rendering without exposing render targets or GPU state to
 * the Haxe style system.
 */
class InkOverflow {
	public final left:Float;
	public final top:Float;
	public final right:Float;
	public final bottom:Float;

	public function new(left:Float = 0.0, top:Float = 0.0, right:Float = 0.0,
			bottom:Float = 0.0) {
		for (value in [left, top, right, bottom])
			if (!Math.isFinite(value) || value < 0.0)
				throw "Effect ink overflow must be finite and non-negative";
		this.left = left;
		this.top = top;
		this.right = right;
		this.bottom = bottom;
	}

	public static function zero():InkOverflow
		return new InkOverflow();

	/** Adds sequential effect extents. */
	public function added(other:InkOverflow):InkOverflow {
		if (other == null)
			return this;
		return new InkOverflow(left + other.left, top + other.top,
			right + other.right, bottom + other.bottom);
	}

	public function isEqual(other:InkOverflow):Bool
		return other != null && left == other.left && top == other.top &&
			right == other.right && bottom == other.bottom;

	public function toString():String
		return '($left, $top, $right, $bottom)';
}
