/** One Haxe-owned axis policy encoded into a layout transaction. */
class LayoutAxis {
	public var sizing:LayoutSizing;
	public var value:Float;

	public function new(sizing:LayoutSizing = LayoutSizing.Fit, value:Float = 0.0) {
		this.sizing = sizing;
		this.value = value;
	}

	public static function fit(value:Float = 0.0):LayoutAxis
		return new LayoutAxis(LayoutSizing.Fit, value);

	public static function grow(value:Float = 0.0):LayoutAxis
		return new LayoutAxis(LayoutSizing.Grow, value);

	public static function fixed(value:Float):LayoutAxis
		return new LayoutAxis(LayoutSizing.Fixed, value);

	public static function percent(value:Float):LayoutAxis
		return new LayoutAxis(LayoutSizing.Percent, value);
}
