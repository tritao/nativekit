import NativeKitUI;

/** Constraint range supplied while NativeKit measures a custom layout node. */
class LayoutMeasureConstraints {
	public final minWidth:Float;
	public final maxWidth:Float;
	public final minHeight:Float;
	public final maxHeight:Float;

	private function new(minWidth:Float, maxWidth:Float, minHeight:Float, maxHeight:Float) {
		this.minWidth = minWidth;
		this.maxWidth = maxWidth;
		this.minHeight = minHeight;
		this.maxHeight = maxHeight;
	}

	@:allow(LayoutSession)
	private static function fromNative(value:nkui_layout_measure_constraints):LayoutMeasureConstraints {
		return new LayoutMeasureConstraints(value.get_min_width(), value.get_max_width(),
			value.get_min_height(), value.get_max_height());
	}
}
