package nativekit.scene;

/**
 * Compatibility name for source-entity presentation rules.
 *
 * New code can use SceneViewFilter; this subclass keeps existing Haxeon call
 * sites source-compatible while sharing the same implementation.
 */
class SourceEntityFilter extends SceneViewFilter {
	public function new()
		super();
}
