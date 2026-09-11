import NativeKitUI;

/** Immutable normalized RGBA color value. */
class Color {
	public final red:Float;
	public final green:Float;
	public final blue:Float;
	public final alpha:Float;

	public function new(red:Float, green:Float, blue:Float, alpha:Float = 1.0) {
		this.red = red;
		this.green = green;
		this.blue = blue;
		this.alpha = alpha;
	}

	public static function rgba(red:Float, green:Float, blue:Float, alpha:Float = 1.0):Color
		return new Color(red, green, blue, alpha);

	public static function fromBytes(red:Int, green:Int, blue:Int, alpha:Int = 255):Color
		return new Color(red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0);

	@:allow(Paint)
	@:allow(SolidPaint)
	private function nativeValue():nkui_color {
		var result = new nkui_color();
		result.set_red(red);
		result.set_green(green);
		result.set_blue(blue);
		result.set_alpha(alpha);
		return result;
	}
}
