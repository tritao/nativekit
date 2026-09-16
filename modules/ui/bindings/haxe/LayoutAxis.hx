/** One Haxe-owned axis policy encoded into a layout transaction. */
class LayoutAxis {
	public var sizing:LayoutSizing;
	/** Exact value for FIXED, or the parent fraction for PERCENT. */
	public var value:Float;
	/** Minimum size for FIT/GROW. */
	public var min:Float;
	/** Maximum size for FIT/GROW; zero means unbounded. */
	public var max:Float;
	/** Relative share of extra space for GROW; ignored by other sizing modes. */
	public var growWeight:Float;

	public function new(sizing:LayoutSizing = LayoutSizing.Fit, value:Float = 0.0,
			min:Float = 0.0, max:Float = 0.0, growWeight:Float = 1.0) {
		this.sizing = sizing;
		this.value = value;
		this.min = min;
		this.max = max;
		this.growWeight = growWeight;
	}

	public static function fit(min:Float = 0.0, max:Float = 0.0):LayoutAxis
		return new LayoutAxis(LayoutSizing.Fit, 0.0, min, max);

	public static function grow(min:Float = 0.0, max:Float = 0.0, growWeight:Float = 1.0):LayoutAxis
		return new LayoutAxis(LayoutSizing.Grow, 0.0, min, max, growWeight);

	public static function fixed(value:Float):LayoutAxis
		return new LayoutAxis(LayoutSizing.Fixed, value);

	public static function percent(value:Float):LayoutAxis
		return new LayoutAxis(LayoutSizing.Percent, value);

	/** Fills the parent's available size on this axis without consuming main-axis grow space. */
	public static function stretch():LayoutAxis
		return percent(1.0);
}
