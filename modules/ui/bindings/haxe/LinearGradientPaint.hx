import NativeKitUI;

/** Immutable linear gradient paint with up to eight color stops. */
class LinearGradientPaint extends Paint {
	@:allow(LinearGradientPaint)
	private function new(value:nkui_resource)
		super(value);

	public static function create(startX:Float, startY:Float, endX:Float, endY:Float,
		stops:Array<GradientStop>):LinearGradientPaint {
		if (stops == null || stops.length < 2 ||
			stops.length > NativeKitUIConstants.NKUI_GRADIENT_MAX_STOPS)
			throw "Linear gradients require between two and eight stops";
		var nativeStops:Array<nkui_gradient_stop> = [];
		for (stop in stops) {
			if (stop == null)
				throw "Linear gradient stops cannot be null";
			nativeStops.push(stop.nativeValue());
		}
		var made = NativeKitUI.nkui_paint_create_linear_gradient(startX, startY, endX, endY,
			nativeStops);
		UiResult.check(made.status, "paint.createLinearGradient");
		return new LinearGradientPaint(made.out_paint);
	}
}
