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
		for (info in snapshot.occurrences()) {
			var source = info.sourceValue(),
				visibilityIndex = sourceIndex(visibilitySources, source),
				materialIndex = sourceIndex(materialSources, source);
			if (visibilityIndex >= 0)
				policy.setVisibility(info.occurrence(), visibilityValues[visibilityIndex]);
			if (materialIndex >= 0)
				policy.setMaterial(info.occurrence(), materialValues[materialIndex]);
		}
		return policy;
	}

	static function sourceIndex(sources:Array<haxe.Int64>, source:haxe.Int64):Int {
		for (index in 0...sources.length)
			if (sources[index] == source)
				return index;
		return -1;
	}
}
