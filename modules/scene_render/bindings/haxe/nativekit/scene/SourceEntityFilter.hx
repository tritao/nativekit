package nativekit.scene;

/**
 * Describes source-entity presentation rules for a SceneView.
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
	final isolatedSources:Array<haxe.Int64> = [];

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

	/** Replaces the isolation set with one source. */
	public function isolateSource(source:haxe.Int64):SourceEntityFilter {
		isolatedSources.resize(0);
		isolatedSources.push(source);
		return this;
	}

	/** Replaces the isolation set with the supplied sources. */
	public function isolateSources(sources:Array<haxe.Int64>):SourceEntityFilter {
		isolatedSources.resize(0);
		for (source in sources) {
			var found = false;
			for (existing in isolatedSources)
				if (existing == source) {
					found = true;
					break;
				}
			if (!found)
				isolatedSources.push(source);
		}
		return this;
	}

	public function clearIsolation():SourceEntityFilter {
		isolatedSources.resize(0);
		return this;
	}

	public function clear():SourceEntityFilter {
		visibilitySources.resize(0);
		visibilityValues.resize(0);
		materialSources.resize(0);
		materialValues.resize(0);
		isolatedSources.resize(0);
		return this;
	}

	public function visibilityRuleCount():Int
		return visibilitySources.length;

	public function materialRuleCount():Int
		return materialSources.length;

	public function isolationRuleCount():Int
		return isolatedSources.length;

	/** Applies declarative source rules to a view policy. */
	public function apply(snapshot:Snapshot, policy:SceneViewPolicy):SceneViewPolicy {
		for (index in 0...visibilitySources.length) {
			policy.setSourceVisibility(visibilitySources[index], visibilityValues[index]);
		}
		for (index in 0...materialSources.length) {
			policy.setSourceMaterial(materialSources[index], materialValues[index]);
		}
		policy.isolateSources(isolatedSources);
		return policy;
	}
}
