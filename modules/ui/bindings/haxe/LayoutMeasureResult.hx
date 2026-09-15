import NativeKitUI;

/** Intrinsic size and optional baseline returned for a custom layout node. */
class LayoutMeasureResult {
	public final width:Float;
	public final height:Float;
	public final baseline:Float;
	public final hasBaseline:Bool;

	public function new(width:Float, height:Float, ?baseline:Float = 0.0,
			hasBaseline:Bool = false) {
		if (width < 0.0 || height < 0.0 || width != width || height != height)
			throw "Measured dimensions must be finite and non-negative";
		if (hasBaseline && (baseline < 0.0 || baseline > height || baseline != baseline))
			throw "Measured baseline must be within the measured height";
		this.width = width;
		this.height = height;
		this.baseline = baseline;
		this.hasBaseline = hasBaseline;
	}

	@:allow(LayoutSession)
	private function nativeValue():nkui_layout_measure_result {
		var result = new nkui_layout_measure_result();
		result.set_width(width);
		result.set_height(height);
		result.set_baseline(baseline);
		result.set_flags(hasBaseline ? 1 : 0);
		return result;
	}
}
