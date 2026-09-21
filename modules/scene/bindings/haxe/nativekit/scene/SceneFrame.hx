package nativekit.scene;

/**
 * Owns the immutable snapshot and optional change set for one renderable
 * scene frame.
 *
 * A frame keeps both native values alive until the render boundary has
 * consumed them. The change set may be null for an initial or explicitly
 * refreshed frame.
 */
class SceneFrame {
	final snapshotValue:Snapshot;
	final changesValue:Null<ChangeSet>;
	var disposed:Bool = false;

	@:allow(Scene, Transaction)
	private function new(snapshot:Snapshot, changes:Null<ChangeSet>) {
		snapshotValue = snapshot;
		changesValue = changes;
	}

	/** Creates a frame from the scene's current published state. */
	public static function snapshot(scene:Scene):SceneFrame
		return new SceneFrame(scene.snapshot(), null);

	/** Returns the immutable scene state retained by this frame. */
	public function sceneSnapshot():Snapshot {
		ensureLive();
		return snapshotValue;
	}

	/** Returns the commit delta, or null when this frame has no delta. */
	public function changeSet():Null<ChangeSet> {
		ensureLive();
		return changesValue;
	}

	/** Releases the change set and snapshot in dependency-safe order. */
	public function dispose():Void {
		if (disposed)
			return;
		if (changesValue != null)
			changesValue.dispose();
		snapshotValue.dispose();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Scene frame has been disposed";
	}
}
