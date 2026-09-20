package nativekit.scene;

/** Reusable per-occurrence visibility policy for a SceneView. */
class VisibilityFilter {
	final entries:Array<{occurrence:Occurrence, visible:Bool}> = [];

	public function new() {}

	public function set(occurrence:Occurrence, visible:Bool):VisibilityFilter {
		var stable = occurrence.stableValue();
		for (entry in entries) {
			if (entry.occurrence.stableValue() == stable) {
				entry.visible = visible;
				return this;
			}
		}
		entries.push({occurrence: occurrence, visible: visible});
		return this;
	}

	public function show(occurrence:Occurrence):VisibilityFilter
		return set(occurrence, true);

	public function hide(occurrence:Occurrence):VisibilityFilter
		return set(occurrence, false);

	public function clear():VisibilityFilter {
		entries.resize(0);
		return this;
	}

	public function count():Int
		return entries.length;

	@:allow(SceneView)
	function apply(view:SceneView):Void {
		for (entry in entries)
			view.setVisibility(entry.occurrence, entry.visible);
	}
}
