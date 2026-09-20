package nativekit.scene;

/**
 * Converts source-entity rules into occurrence-level view policy rules.
 *
 * Source IDs can be shared by many occurrences, so this is useful for CAD/BIM
 * selection and isolation without changing the Scene itself. The snapshot is
 * read only; the resulting rules are applied to a SceneViewPolicy.
 */
class SourceEntityFilter {
	final visibilitySources:Array<haxe.Int64> = [];
	final visibilityValues:Array<Bool> = [];
	final materialSources:Array<haxe.Int64> = [];
	final materialValues:Array<Material> = [];

	public function new() {}

	public function setSourceVisibility(source:haxe.Int64, visible:Bool):SourceEntityFilter {
		for (index in 0...visibilitySources.length) {
			if (visibilitySources[index] == source) {
				visibilityValues[index] = visible;
				return this;
			}
		}
		visibilitySources.push(source);
		visibilityValues.push(visible);
		return this;
	}

	public function hideSource(source:haxe.Int64):SourceEntityFilter
		return setSourceVisibility(source, false);

	public function showSource(source:haxe.Int64):SourceEntityFilter
		return setSourceVisibility(source, true);

	public function setSourceMaterial(source:haxe.Int64,
			material:Material):SourceEntityFilter {
		for (index in 0...materialSources.length) {
			if (materialSources[index] == source) {
				materialValues[index] = material;
				return this;
			}
		}
		materialSources.push(source);
		materialValues.push(material);
		return this;
	}

	public function clear():SourceEntityFilter {
		visibilitySources.resize(0);
		visibilityValues.resize(0);
		materialSources.resize(0);
		materialValues.resize(0);
		return this;
	}

	public function visibilityRuleCount():Int
		return visibilitySources.length;

	public function materialRuleCount():Int
		return materialSources.length;

	/** Applies the current source rules to matching occurrences in a snapshot. */
	public function apply(snapshot:Snapshot, policy:SceneViewPolicy):SceneViewPolicy {
		for (index in 0...visibilitySources.length) {
			for (occurrence in snapshot.occurrencesForSource(visibilitySources[index]))
				policy.setVisibility(occurrence, visibilityValues[index]);
		}
		for (index in 0...materialSources.length) {
			for (occurrence in snapshot.occurrencesForSource(materialSources[index]))
				policy.setMaterial(occurrence, materialValues[index]);
		}
		return policy;
	}
}
