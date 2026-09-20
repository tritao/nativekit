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
		check(result.status, "snapshot.revision");
		return result.out_revision;
	}

	public function occurrenceCount():Int {
		ensureLive();
		var result = NativeKitScene.nkscene_snapshot_get_occurrence_count(owner.borrow());
		check(result.status, "snapshot.occurrenceCount");
		return haxe.Int64.toInt(result.out_count);
	}

	public function occurrenceAt(index:Int):OccurrenceInfo {
		ensureLive();
		if (index < 0)
			throw "Snapshot occurrence index cannot be negative";
		var value = new nkscene_snapshot_occurrence();
		value.set_struct_size(nkscene_snapshot_occurrence.size());
		var result = NativeKitScene.nkscene_snapshot_get_occurrence(owner.borrow(), index, value);
		check(result.status, "snapshot.occurrenceAt");
		return new OccurrenceInfo(value);
	}

	public function occurrences():Array<OccurrenceInfo> {
		var result:Array<OccurrenceInfo> = [];
		var count = occurrenceCount();
		for (index in 0...count)
			result.push(occurrenceAt(index));
		return result;
	}

	public function find(occurrence:Occurrence):Null<OccurrenceInfo> {
		for (info in occurrences())
			if (info.occurrence().equals(occurrence))
				return info;
		return null;
	}

	public function children(parent:Occurrence):Array<Occurrence> {
		var result:Array<Occurrence> = [];
		for (info in occurrences()) {
			var candidate = info.parent();
			if (candidate != null && candidate.equals(parent))
				result.push(info.occurrence());
		}
		return result;
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

	static function check(status:Int, operation:String):Void {
		if (status != NativeKitSceneConstants.NKS_OK)
			throw '$operation failed with NativeKit scene status $status';
	}
}
