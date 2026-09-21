package nativekit.ui.core;

/** Document-owned history and savepoint state for editor applications. */
class EditorDocument {
	public final id:String;
	public final history:EditHistory;
	var savedStateToken:Int;
	var externallyDirty:Bool;

	public function new(id:String, ?history:EditHistory) {
		if (id == null || id.length == 0)
			throw "Editor documents require a stable ID";
		this.id = id;
		this.history = history == null ? new EditHistory() : history;
		savedStateToken = this.history.stateToken;
		externallyDirty = false;
	}

	public var revision(get, never):Int;
	inline function get_revision():Int
		return history.revision;

	public var isDirty(get, never):Bool;
	inline function get_isDirty():Bool
		return externallyDirty || history.stateToken != savedStateToken;

	public var canUndo(get, never):Bool;
	inline function get_canUndo():Bool
		return history.canUndo;

	public var canRedo(get, never):Bool;
	inline function get_canRedo():Bool
		return history.canRedo;

	public function apply(operation:EditOperation, ?coalesceKey:String):Bool
		return history.apply(operation, coalesceKey);

	public function record(operation:EditOperation, ?coalesceKey:String):Bool
		return history.record(operation, coalesceKey);

	public function begin(label:String, ?coalesceKey:String):EditTransaction
		return history.begin(label, coalesceKey);

	public function undo():Bool
		return history.undo();

	public function redo():Bool
		return history.redo();

	/** Declares that an external model mutation must be included in the next save. */
	public function markExternallyDirty():Void
		externallyDirty = true;

	/** Marks the current document state as persisted. */
	public function markSaved():Void {
		savedStateToken = history.stateToken;
		externallyDirty = false;
	}

	public function clearHistory():Void
		history.clear();
}
