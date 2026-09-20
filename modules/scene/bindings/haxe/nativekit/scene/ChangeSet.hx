package nativekit.scene;

import NativeKitScene;

/** Immutable transaction result describing the changed scene domains. */
class ChangeSet {
	final owner:Ownednkscene_change_set;
	var disposed:Bool = false;

	@:allow(Transaction)
	private function new(owner:Ownednkscene_change_set) {
		this.owner = owner;
	}

	public function nativeHandle():nkscene_change_set {
		ensureLive();
		return owner.borrow();
	}

	public function revision():haxe.Int64 {
		ensureLive();
		var result = NativeKitScene.nkscene_change_set_get_revision(owner.borrow());
		if (result.status != NativeKitSceneConstants.NKS_OK)
			throw 'changeSet.revision failed with NativeKit scene status ${result.status}';
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
			throw "Scene change set has been disposed";
	}
}
