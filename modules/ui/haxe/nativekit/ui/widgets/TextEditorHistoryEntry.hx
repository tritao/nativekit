package nativekit.ui.widgets;

/** One undoable document edit, with a mutable end for coalesced input. */
class TextEditorHistoryEntry {
	public final before:TextEditorSnapshot;
	public var after:TextEditorSnapshot;
	public final kind:TextEditorHistoryKind;

	public function new(before:TextEditorSnapshot, after:TextEditorSnapshot,
			kind:TextEditorHistoryKind) {
		this.before = before;
		this.after = after;
		this.kind = kind;
	}
}
