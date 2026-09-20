package nativekit.scene;

import NativeKitScene;

/** Read-only axis-aligned bounds captured by a scene snapshot. */
class Bounds {
	public final valid:Bool;
	public final minX:Float;
	public final minY:Float;
	public final minZ:Float;
	public final maxX:Float;
	public final maxY:Float;
	public final maxZ:Float;

	private function new(value:nkscene_bounds) {
		valid = value.get_valid() != 0;
		minX = value.get_minimum(0);
		minY = value.get_minimum(1);
		minZ = value.get_minimum(2);
		maxX = value.get_maximum(0);
		maxY = value.get_maximum(1);
		maxZ = value.get_maximum(2);
	}

	@:allow(OccurrenceInfo)
	static function fromNative(value:nkscene_bounds):Bounds
		return new Bounds(value);
}
