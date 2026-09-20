package nativekit.scene;

import NativeKitScene;

/** Immutable read-only scene state captured at one scene revision. */
class Snapshot {
	final owner:Ownednkscene_snapshot;
	var disposed:Bool = false;

	@:allow(Scene)
	private function new(owner:Ownednkscene_snapshot) {
		this.owner = owner;
	}

	public function nativeHandle():nkscene_snapshot {
		ensureLive();
		return owner.borrow();
	}

	public function revision():haxe.Int64 {
		ensureLive();
		var result = NativeKitScene.nkscene_snapshot_get_revision(owner.borrow());
		if (result.status != NativeKitSceneConstants.NKS_OK)
			throw 'snapshot.revision failed with NativeKit scene status ${result.status}';
		return result.out_revision;
	}

	public function dispose():Void {
		if (disposed)
			return;
		owner.close();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function ensureLive():Void {
		if (disposed)
			throw "Scene snapshot has been disposed";
	}
}
