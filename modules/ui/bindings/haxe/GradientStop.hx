import NativeKitUI;

/** Immutable position and color pair used by a gradient paint. */
class GradientStop {
	public final offset:Float;
	public final color:Color;

	public function new(offset:Float, color:Color) {
		if (offset < 0.0 || offset > 1.0 || color == null)
			throw "Gradient stop offset must be in the range 0..1 and include a color";
		this.offset = offset;
		this.color = color;
	}

	@:allow(LinearGradientPaint)
	private function nativeValue():nkui_gradient_stop {
		var result = new nkui_gradient_stop();
		result.set_offset(offset);
		result.set_color(color.nativeValue());
		return result;
	}
}
