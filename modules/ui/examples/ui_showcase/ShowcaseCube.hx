import NativeKitUIShowcase;

/** Showcase-only typed facade for the native rotating-cube surface producer. */
class ShowcaseCube {
	public static function create():GraphicsSurface {
		var made = NativeKitUIShowcase.nkui_showcase_cube_create();
		UiResult.check(made.status, "showcaseCube.create");
		return new GraphicsSurface(made.out_surface);
	}

	public static function setRotation(surface:GraphicsSurface, radians:Float):Void {
		if (surface == null || surface.isDisposed())
			throw "showcaseCube.setRotation requires a live surface";
		UiResult.check(NativeKitUIShowcase.nkui_showcase_cube_set_rotation(surface.nativeHandle(),
			radians), "showcaseCube.setRotation");
	}
}
