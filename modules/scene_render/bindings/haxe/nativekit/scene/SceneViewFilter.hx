package nativekit.scene;

/**
 * Native scene-view presentation filter.
 *
 * SourceEntityFilter remains available as a compatibility subclass for code
 * written before this value had its SceneView-oriented name.
 */
class SceneViewFilter {
	final visibilitySources:Array<haxe.Int64> = [];
	final visibilityValues:Array<Bool> = [];
	final materialSources:Array<haxe.Int64> = [];
	final materialValues:Array<Material> = [];
	final isolatedSources:Array<haxe.Int64> = [];

	public function new() {}

	public function setSourceVisibility(source:haxe.Int64, visible:Bool):SceneViewFilter {
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

	public function hideSource(source:haxe.Int64):SceneViewFilter
		return setSourceVisibility(source, false);

	public function showSource(source:haxe.Int64):SceneViewFilter
		return setSourceVisibility(source, true);

	public function setSourceMaterial(source:haxe.Int64,
			material:Material):SceneViewFilter {
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
	public function isolateSource(source:haxe.Int64):SceneViewFilter {
		isolatedSources.resize(0);
		isolatedSources.push(source);
		return this;
	}

	/** Replaces the isolation set with the supplied sources. */
	public function isolateSources(sources:Array<haxe.Int64>):SceneViewFilter {
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

	public function clearIsolation():SceneViewFilter {
		isolatedSources.resize(0);
		return this;
	}

	public function clear():SceneViewFilter {
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

	/** Applies the source rules directly to a reusable view. */
	public function applyTo(view:SceneView):SceneView {
		view.clearSourceFilter();
		for (index in 0...visibilitySources.length)
			view.setSourceVisibility(visibilitySources[index], visibilityValues[index]);
		for (index in 0...materialSources.length)
			view.setSourceMaterial(materialSources[index], materialValues[index]);
		for (source in isolatedSources)
			view.setIsolatedSource(source, true);
		return view;
	}

	/** Applies declarative source rules to a view policy. */
	public function apply(snapshot:Snapshot, policy:SceneViewPolicy):SceneViewPolicy {
		for (index in 0...visibilitySources.length)
			policy.setSourceVisibility(visibilitySources[index], visibilityValues[index]);
		for (index in 0...materialSources.length)
			policy.setSourceMaterial(materialSources[index], materialValues[index]);
		policy.isolateSources(isolatedSources);
		return policy;
	}
}
