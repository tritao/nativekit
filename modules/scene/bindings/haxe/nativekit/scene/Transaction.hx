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

	public function createOccurrence():Occurrence {
		ensureOpen();
		var occurrence = new nkscene_occurrence_id();
		check(NativeKitScene.nkscene_tx_create_occurrence(owner.borrow(), occurrence),
			"transaction.createOccurrence");
		return new Occurrence(occurrence);
	}

	public function destroyOccurrence(occurrence:Occurrence):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_destroy_occurrence(owner.borrow(), occurrence.nativeValue()),
			"transaction.destroyOccurrence");
	}

	public function setParent(occurrence:Occurrence, parent:Null<Occurrence>):Void {
		ensureOpen();
		var parentValue = parent == null ? new nkscene_occurrence_id() : parent.nativeValue();
		check(NativeKitScene.nkscene_tx_set_parent(owner.borrow(), occurrence.nativeValue(), parentValue),
			"transaction.setParent");
	}

	public function setTransform(occurrence:Occurrence, transform:Transform):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_transform(owner.borrow(), occurrence.nativeValue(),
			transform.nativeValue()),
			"transaction.setTransform");
	}

	public function setTransforms(updates:Array<TransformUpdate>):Void {
		ensureOpen();
		var values:Array<nkscene_transform_update> = [];
		for (update in updates)
			values.push(update.nativeValue());
		check(NativeKitScene.nkscene_tx_set_transforms(owner.borrow(), values),
			"transaction.setTransforms");
	}

	public function setName(occurrence:Occurrence, name:String):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_name(owner.borrow(), occurrence.nativeValue(), name),
			"transaction.setName");
	}

	public function setEntityName(entity:haxe.Int64, name:String):Void {
		ensureOpen();
		var value = new nkscene_entity_id();
		value.set_value(entity);
		check(NativeKitScene.nkscene_tx_set_entity_name(owner.borrow(), value, name),
			"transaction.setEntityName");
	}

	public function setGeometry(occurrence:Occurrence, geometry:Geometry):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_geometry(owner.borrow(), occurrence.nativeValue(), geometry.id()),
			"transaction.setGeometry");
	}

	public function setMaterial(occurrence:Occurrence, material:Material):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_material(owner.borrow(), occurrence.nativeValue(), material.id()),
			"transaction.setMaterial");
	}

	public function setCamera(occurrence:Occurrence, camera:Null<Camera>):Void {
		ensureOpen();
		var value = camera == null ? new nkscene_camera_id() : camera.id();
		check(NativeKitScene.nkscene_tx_set_camera(owner.borrow(), occurrence.nativeValue(), value),
			"transaction.setCamera");
	}

	public function setLight(occurrence:Occurrence, light:Null<Light>):Void {
		ensureOpen();
		var value = light == null ? new nkscene_light_id() : light.id();
		check(NativeKitScene.nkscene_tx_set_light(owner.borrow(), occurrence.nativeValue(), value),
			"transaction.setLight");
	}

	public function setVisibility(occurrence:Occurrence, visible:Bool):Void {
		ensureOpen();
		check(NativeKitScene.nkscene_tx_set_visibility(owner.borrow(), occurrence.nativeValue(),
			visible ? 1 : 0),
			"transaction.setVisibility");
	}

	/** Associates an occurrence with a source entity; zero clears the association. */
	public function setSourceEntity(occurrence:Occurrence, source:haxe.Int64):Void {
		ensureOpen();
		var entity = new nkscene_entity_id();
		entity.set_value(source);
		check(NativeKitScene.nkscene_tx_set_source_entity(owner.borrow(), occurrence.nativeValue(), entity),
			"transaction.setSourceEntity");
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

	/** Commits this transaction and retains its snapshot/delta as one frame. */
	public function commitFrame():SceneFrame {
		ensureOpen();
		var changes = commitWithChanges();
		try {
			return new SceneFrame(scene.snapshot(), changes);
		} catch (error:Dynamic) {
			changes.dispose();
			throw error;
		}
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
