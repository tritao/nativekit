package nativekit.ui.widgets;

/** One undoable document edit, with a mutable end for coalesced input. */
class TextEditorHistoryEntry {
	public final before:TextEditorSnapshot;
	public var after:TextEditorSnapshot;
	public final kind:TextEditorHistoryKind;
	/** Replacement range in the before snapshot. */
	public var beforeEditStart:Int;
	public var beforeEditEnd:Int;
	public var beforeEditText:String;
	/** Replacement range in the after snapshot. */
	public var afterEditStart:Int;
	public var afterEditEnd:Int;
	public var afterEditText:String;

	public function new(before:TextEditorSnapshot, after:TextEditorSnapshot,
			kind:TextEditorHistoryKind, beforeEditStart:Int = -1,
			beforeEditEnd:Int = -1, beforeEditText:String = "",
			afterEditStart:Int = -1, afterEditEnd:Int = -1, afterEditText:String = "") {
		this.before = before;
		this.after = after;
		this.kind = kind;
		this.beforeEditStart = beforeEditStart;
		this.beforeEditEnd = beforeEditEnd;
		this.beforeEditText = beforeEditText == null ? "" : beforeEditText;
		this.afterEditStart = afterEditStart;
		this.afterEditEnd = afterEditEnd;
		this.afterEditText = afterEditText == null ? "" : afterEditText;
	}
}
