package nativekit.scene;

/**
 * Reusable base presentation rules for a SceneView.
 *
 * Policies are composable: apply them in order and a later rule for the same
 * occurrence replaces an earlier rule in the same layer. Selection and hover
 * are intentionally not part of this layer; ScenePresentation owns those
 * transient interaction layers.
 */
class SceneViewPolicy {
	final visibility:VisibilityFilter;
	final materialOccurrences:Array<Occurrence> = [];
	final materialValues:Array<Material> = [];

	public function new() {
		visibility = new VisibilityFilter();
	}

	public function setVisibility(occurrence:Occurrence, visible:Bool):SceneViewPolicy {
		visibility.set(occurrence, visible);
		return this;
	}

	public function hide(occurrence:Occurrence):SceneViewPolicy
		return setVisibility(occurrence, false);

	public function show(occurrence:Occurrence):SceneViewPolicy
		return setVisibility(occurrence, true);

	/** Visibility follows the occurrence hierarchy, so this affects descendants. */
	public function hideSubtree(occurrence:Occurrence):SceneViewPolicy
		return hide(occurrence);

	/** Restores this subtree unless another ancestor policy keeps it hidden. */
	public function showSubtree(occurrence:Occurrence):SceneViewPolicy
		return show(occurrence);

	public function setMaterial(occurrence:Occurrence, material:Material):SceneViewPolicy {
		var stable = occurrence.stableValue();
		for (index in 0...materialOccurrences.length) {
			if (materialOccurrences[index].stableValue() == stable) {
				materialValues[index] = material;
				return this;
			}
		}
		materialOccurrences.push(occurrence);
		materialValues.push(material);
		return this;
	}

	public function clear():SceneViewPolicy {
		visibility.clear();
		materialOccurrences.resize(0);
		materialValues.resize(0);
		return this;
	}

	public function visibilityCount():Int
		return visibility.count();

	public function materialCount():Int
		return materialOccurrences.length;

	public function applySourceFilter(snapshot:Snapshot,
			filter:SourceEntityFilter):SceneViewPolicy
		return filter.apply(snapshot, this);

	/** Applies this policy without disturbing other view layers. */
	public function apply(view:SceneView):SceneView {
		view.applyVisibilityFilter(visibility);
		for (index in 0...materialOccurrences.length)
			view.setMaterial(materialOccurrences[index], materialValues[index]);
		return view;
	}
}
