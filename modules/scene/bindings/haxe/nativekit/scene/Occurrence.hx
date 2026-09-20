package nativekit.scene;

import NativeKitScene;

/** Stable scene occurrence identity returned by a transaction. */
class Occurrence {
	final value:nkscene_occurrence_id;

	@:allow(Transaction)
	private function new(value:nkscene_occurrence_id) {
		this.value = value;
	}

	@:allow(PickResult, OccurrenceInfo)
	static function fromNative(value:nkscene_occurrence_id):Occurrence
		return new Occurrence(value);

	/** Returns the stable 64-bit occurrence value for logging or maps. */
	public function stableValue():haxe.Int64
		return value.get_value();

	public function equals(other:Occurrence):Bool
		return stableValue() == other.stableValue();

	@:allow(Transaction, SceneView, VisibilityFilter, SelectionSet, SceneRenderer)
	function nativeValue():nkscene_occurrence_id
		return value;
}
