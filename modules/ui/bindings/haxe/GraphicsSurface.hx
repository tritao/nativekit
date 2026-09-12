import NativeKitUI;
import NativeKitUIShowcase;

/** Typed compositable surface produced by NativeKit's renderer. */
class GraphicsSurface extends NativeKitUIResource {
	private function new(value:nkui_resource)
		super(value);

	/** Creates the animated 3D surface used by the Graphics Lab example. */
	public static function createShowcaseCube():GraphicsSurface {
		var made = NativeKitUIShowcase.nkui_showcase_cube_create();
		UiResult.check(made.status, "graphicsSurface.createShowcaseCube");
		return new GraphicsSurface(made.out_surface);
	}

	/** Sets the cube's angle; the producer redraws only when this value changes. */
	public function setShowcaseCubeRotation(radians:Float):Void {
		UiResult.check(NativeKitUIShowcase.nkui_showcase_cube_set_rotation(nativeHandle(), radians),
			"graphicsSurface.setShowcaseCubeRotation");
	}
}
