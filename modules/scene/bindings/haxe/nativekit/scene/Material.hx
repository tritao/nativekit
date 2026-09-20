package nativekit.scene;

import NativeKitScene;

/** Owns one material resource ID in a Scene. */
class Material {
	final scene:Scene;
	final value:nkscene_material_id;
	var disposed:Bool = false;

	@:allow(Scene)
	private function new(scene:Scene, value:nkscene_material_id) {
		this.scene = scene;
		this.value = value;
		scene.registerResource(dispose);
	}

	public function id():nkscene_material_id {
		ensureLive();
		return value;
	}

	public function dispose():Void {
		if (disposed)
			return;
		NativeKitScene.nkscene_material_destroy(scene.nativeHandle(), value);
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Material has been disposed";
		scene.ensureLive();
	}
}
