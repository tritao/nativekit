import NativeKitUI;

/** Base class for immutable paint resources. */
class Paint extends NativeKitUIResource {
	function new(value:nkui_resource)
		super(value);
}

/** A uniform RGBA paint. */
class SolidPaint extends Paint {
	@:allow(SolidPaint)
	private function new(value:nkui_resource)
		super(value);

	public static function create(color:Color):SolidPaint {
		var made = NativeKitUI.nkui_paint_create_solid(color.nativeValue());
		UiResult.check(made.status, "paint.createSolid");
		return new SolidPaint(made.out_paint);
	}
}
