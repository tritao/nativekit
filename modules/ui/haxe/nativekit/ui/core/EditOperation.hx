package nativekit.ui.core;

/** One reversible application mutation owned by EditHistory. */
class EditOperation {
	public final label:String;
	public final coalesceKey:Null<String>;
	/** Optional caller-owned value used by a coalescing callback. */
	public final mergeData:Dynamic;
	var applyAction:Void->Void;
	var undoAction:Void->Void;
	final mergeAction:Null<EditOperation->Bool>;
	@:allow(nativekit.ui.core.EditHistory)
	var transactionOperations:Null<Array<EditOperation>>;
	@:allow(nativekit.ui.core.EditHistory)
	var beforeStateToken:Int;
	@:allow(nativekit.ui.core.EditHistory)
	var afterStateToken:Int;

	public function new(label:String, apply:Void->Void, undo:Void->Void,
			?coalesceKey:String, ?merge:EditOperation->Bool, ?mergeData:Dynamic) {
		if (label == null || label.length == 0 || apply == null || undo == null)
			throw "Edit operations require a label, apply action, and undo action";
		if (coalesceKey != null && coalesceKey.length == 0)
			throw "Edit coalescing keys cannot be empty";
		this.label = label;
		this.applyAction = apply;
		this.undoAction = undo;
		this.coalesceKey = coalesceKey;
		this.mergeAction = merge;
		this.mergeData = mergeData;
		transactionOperations = null;
		beforeStateToken = -1;
		afterStateToken = -1;
	}

	public function apply():Void
		applyAction();

	public function undo():Void
		undoAction();

	/**
	 * Replaces the actions retained by this operation after a successful merge.
	 * The merge callback can use this to keep the original undo point while
	 * extending the operation's final state.
	 */
	public function replaceActions(apply:Void->Void, undo:Void->Void):Void {
		if (apply == null || undo == null)
			throw "Merged edit actions cannot be null";
		applyAction = apply;
		undoAction = undo;
	}

	@:allow(nativekit.ui.core.EditHistory)
	function mergeFrom(next:EditOperation):Bool {
		return mergeAction != null && next != null && mergeAction(next);
	}
}
