package nativekit.scene;

import NativeKitScene;

/** Immutable read-only scene state captured at one scene revision. */
class Snapshot {
	final owner:Ownednkscene_snapshot;
	var disposed:Bool = false;
	var occurrenceCache:Null<Array<OccurrenceInfo>> = null;
	var occurrenceIndex:Null<Map<String, OccurrenceInfo>> = null;
	var childrenIndex:Null<Map<String, Array<Occurrence>>> = null;
	var sourceOccurrenceIndex:Null<Map<String, Array<Occurrence>>> = null;

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
		var cached = occurrenceCache;
		if (cached != null) {
			if (index >= cached.length)
				throw "Snapshot occurrence index is out of range";
			return cached[index];
		}
		var value = new nkscene_snapshot_occurrence();
		value.set_struct_size(nkscene_snapshot_occurrence.size());
		var result = NativeKitScene.nkscene_snapshot_get_occurrence(owner.borrow(), index, value);
		check(result.status, "snapshot.occurrenceAt");
		return new OccurrenceInfo(value);
	}

	public function occurrences():Array<OccurrenceInfo> {
		return ensureOccurrenceCache();
	}

	public function find(occurrence:Occurrence):Null<OccurrenceInfo> {
		ensureOccurrenceCache();
		return occurrenceIndex.get(key(occurrence));
	}

	public function children(parent:Occurrence):Array<Occurrence> {
		ensureOccurrenceCache();
		var result = childrenIndex.get(key(parent));
		return result == null ? [] : result.copy();
	}

	/** Returns cached occurrences associated with one source entity. */
	public function occurrencesForSource(source:haxe.Int64):Array<Occurrence> {
		ensureLive();
		if (sourceOccurrenceIndex == null)
			sourceOccurrenceIndex = new Map();
		var sourceKey = haxe.Int64.toStr(source),
			cached = sourceOccurrenceIndex.get(sourceKey);
		if (cached != null)
			return cached;

		var entity = new nkscene_entity_id();
		entity.set_value(source);
		var countResult = NativeKitScene.nkscene_snapshot_get_source_occurrence_count(
			owner.borrow(), entity);
		check(countResult.status, "snapshot.sourceOccurrenceCount");
		var result:Array<Occurrence> = [];
		for (index in 0...haxe.Int64.toInt(countResult.out_count)) {
		var value = NativeKitScene.nkscene_snapshot_get_source_occurrence(
				owner.borrow(), entity, index);
			check(value.status, "snapshot.sourceOccurrence");
			result.push(Occurrence.fromNative(value.out_occurrence));
		}
		sourceOccurrenceIndex.set(sourceKey, result);
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

	function ensureOccurrenceCache():Array<OccurrenceInfo> {
		var result = occurrenceCache;
		if (result != null)
			return result;

		result = [];
		var byValue:Map<String, OccurrenceInfo> = new Map(),
			byParent:Map<String, Array<Occurrence>> = new Map(),
			count = occurrenceCount(),
			pageSize = NativeKitSceneConstants.NKS_SCENE_SNAPSHOT_OCCURRENCE_PAGE_CAPACITY,
			page = new nkscene_snapshot_occurrence_page();
		page.set_struct_size(nkscene_snapshot_occurrence_page.size());
		var offset = 0;
		while (offset < count) {
			var pageResult = NativeKitScene.nkscene_snapshot_get_occurrence_page(
				owner.borrow(), offset, page);
			check(pageResult.status, "snapshot.occurrencePage");
			var pageCount = page.get_count();
			if (pageCount <= 0 || pageCount > pageSize || pageCount > count - offset)
				throw "Snapshot occurrence page returned an invalid count";
			for (index in 0...pageCount) {
				var info = new OccurrenceInfo(page.get_occurrences(index));
				result.push(info);
				byValue.set(key(info.occurrence()), info);
				var parent = info.parent();
				if (parent != null) {
					var children = byParent.get(key(parent));
					if (children == null) {
						children = [];
						byParent.set(key(parent), children);
					}
					children.push(info.occurrence());
				}
			}
			offset += pageCount;
		}
		occurrenceCache = result;
		occurrenceIndex = byValue;
		childrenIndex = byParent;
		return result;
	}

	static function key(occurrence:Occurrence):String
		return haxe.Int64.toStr(occurrence.stableValue());
}
