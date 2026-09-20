package nativekit.scene;

import NativeKitScene;

/** Owns one geometry resource ID in a Scene. */
class Geometry {
	final scene:Scene;
	final value:nkscene_geometry_id;
	var disposed:Bool = false;

	@:allow(Scene)
	private function new(scene:Scene, value:nkscene_geometry_id) {
		this.scene = scene;
		this.value = value;
		scene.registerResource(dispose);
	}

	public function id():nkscene_geometry_id {
		ensureLive();
		return value;
	}

	public function dispose():Void {
		if (disposed)
			return;
		NativeKitScene.nkscene_geometry_destroy(scene.nativeHandle(), value);
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Geometry has been disposed";
		scene.ensureLive();
	}
}
