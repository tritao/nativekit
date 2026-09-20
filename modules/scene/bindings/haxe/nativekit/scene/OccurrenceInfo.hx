package nativekit.scene;

import NativeKitScene;

/** Read-only occurrence state captured by a scene snapshot. */
class OccurrenceInfo {
	final occurrenceValue:Occurrence;
	final parentValue:Null<Occurrence>;
	final sourceId:haxe.Int64;
	final geometryId:haxe.Int64;
	final materialId:haxe.Int64;
	final visibleValue:Bool;
	final localTransformValue:Transform;
	final worldTransformValue:Transform;
	final worldTransformRevisionValue:haxe.Int64;
	final boundsValue:Bounds;

	@:allow(Snapshot)
	private function new(value:nkscene_snapshot_occurrence) {
		occurrenceValue = Occurrence.fromNative(value.get_occurrence());
		var parent = value.get_parent();
		parentValue = haxe.Int64.toInt(parent.get_value()) == 0 ? null
			: Occurrence.fromNative(parent);
		sourceId = value.get_source().get_value();
		geometryId = value.get_geometry().get_value();
		materialId = value.get_material().get_value();
		visibleValue = value.get_visible() != 0;
		localTransformValue = Transform.fromNative(value.get_local_transform());
		worldTransformValue = Transform.fromNative(value.get_world_transform());
		worldTransformRevisionValue = value.get_world_transform_revision();
		boundsValue = Bounds.fromNative(value.get_bounds());
	}

	public function occurrence():Occurrence
		return occurrenceValue;

	public function parent():Null<Occurrence> {
		return parentValue;
	}

	public function sourceValue():haxe.Int64
		return sourceId;

	public function geometryValue():haxe.Int64
		return geometryId;

	public function materialValue():haxe.Int64
		return materialId;

	public function visible():Bool
		return visibleValue;

	public function localTransform():Transform
		return localTransformValue;

	public function worldTransform():Transform
		return worldTransformValue;

	public function worldTransformRevision():haxe.Int64
		return worldTransformRevisionValue;

	public function bounds():Bounds
		return boundsValue;
}
