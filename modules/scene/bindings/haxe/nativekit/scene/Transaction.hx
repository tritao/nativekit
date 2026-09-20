package nativekit.scene;

import NativeKitScene;

/** Records scene mutations until commit or cancellation. */
class Transaction {
	final scene:Scene;
	var owner:Ownednkscene_transaction;
	var closed:Bool = false;

	@:allow(Scene)
	private function new(scene:Scene, owner:Ownednkscene_transaction) {
		this.scene = scene;
		this.owner = owner;
	}

	public function createOccurrence():nkscene_occurrence_id {
		ensureOpen();
		var occurrence = new nkscene_occurrence_id();
		check(NativeKitScene.nkscene_tx_create_occurrence(owner.borrow(), occurrence),
			"transaction.createOccurrence");
		return occurrence;
	}

	public function destroyOccurrence(occurrence:nkscene_occurrence_id):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_destroy_occurrence(owner.borrow(), occurrence),
			"transaction.destroyOccurrence");
	}

	public function setParent(occurrence:nkscene_occurrence_id,
			parent:nkscene_occurrence_id):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_parent(owner.borrow(), occurrence, parent),
			"transaction.setParent");
	}

	public function setTransform(occurrence:nkscene_occurrence_id,
			transform:nkscene_transform):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_transform(owner.borrow(), occurrence, transform),
			"transaction.setTransform");
	}

	public function setGeometry(occurrence:nkscene_occurrence_id, geometry:Geometry):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_geometry(owner.borrow(), occurrence, geometry.id()),
			"transaction.setGeometry");
	}

	public function setMaterial(occurrence:nkscene_occurrence_id, material:Material):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_material(owner.borrow(), occurrence, material.id()),
			"transaction.setMaterial");
	}

	public function setVisibility(occurrence:nkscene_occurrence_id, visible:Bool):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_visibility(owner.borrow(), occurrence, visible ? 1 : 0),
			"transaction.setVisibility");
	}

	public function commit():Void {
		ensureOpen();
		check(NativeKitScene.nkscene_transaction_commit(owner.borrow()), "transaction.commit");
		closeOwner();
	}

	public function commitWithChanges():ChangeSet {
		ensureOpen();
		var result = NativeKitScene.nkscene_transaction_commit_with_changes(owner.borrow());
		check(result.status, "transaction.commitWithChanges");
		closeOwner();
		return new ChangeSet(result.out_changes);
	}

	public function cancel():Void {
		if (closed)
			return;
		closeOwner();
	}

	public function dispose():Void
		cancel();

	public function isClosed():Bool
		return closed;

	function ensureOpen():Void {
		if (closed)
			throw "Scene transaction has been closed";
		scene.ensureLive();
	}

	function closeOwner():Void {
		if (closed)
			return;
		owner.close();
		closed = true;
	}

	static function check(status:Int, operation:String):Void {
		if (status != NativeKitSceneConstants.NKS_OK)
			throw '$operation failed with NativeKit scene status $status';
	}
}
