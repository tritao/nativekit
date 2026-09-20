package nativekit.scene;

import NativeKitScene;

/** Read-only occurrence state captured by a scene snapshot. */
class OccurrenceInfo {
	final value:nkscene_snapshot_occurrence;
	final occurrenceValue:Occurrence;

	@:allow(Snapshot)
	private function new(value:nkscene_snapshot_occurrence) {
		this.value = value;
		occurrenceValue = Occurrence.fromNative(value.get_occurrence());
	}

	public function occurrence():Occurrence
		return occurrenceValue;

	public function parent():Null<Occurrence> {
		var parent = value.get_parent();
		return haxe.Int64.toInt(parent.get_value()) == 0 ? null
			: Occurrence.fromNative(parent);
	}

	public function sourceValue():haxe.Int64
		return value.get_source().get_value();

	public function geometryValue():haxe.Int64
		return value.get_geometry().get_value();

	public function materialValue():haxe.Int64
		return value.get_material().get_value();

	public function visible():Bool
		return value.get_visible() != 0;

	public function localTransform():Transform
		return Transform.fromNative(value.get_local_transform());

	public function worldTransform():Transform
		return Transform.fromNative(value.get_world_transform());

	public function worldTransformRevision():haxe.Int64
		return value.get_world_transform_revision();

	public function bounds():Bounds
		return Bounds.fromNative(value.get_bounds());
}
