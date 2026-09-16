package nativekit.ui.widgets;

/** Selection and document state captured at an undo boundary. */
class TextEditorSnapshot {
	public final text:String;
	public final selectionStart:CodepointOffset;
	public final selectionEnd:CodepointOffset;
	public final selectionAnchor:CodepointOffset;
	public final selectionFocus:CodepointOffset;
	public final selectionAnchorLayoutOffset:CodepointOffset;
	public final selectionFocusLayoutOffset:CodepointOffset;
	public final selectionAnchorAffinity:Int;
	public final selectionFocusAffinity:Int;
	public final scrollOffsetY:Float;

	public function new(text:String, selectionStart:CodepointOffset, selectionEnd:CodepointOffset,
			selectionAnchor:CodepointOffset, selectionFocus:CodepointOffset,
			selectionAnchorLayoutOffset:CodepointOffset, selectionFocusLayoutOffset:CodepointOffset,
			selectionAnchorAffinity:Int, selectionFocusAffinity:Int, scrollOffsetY:Float) {
		this.text = text == null ? "" : text;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.selectionAnchor = selectionAnchor;
		this.selectionFocus = selectionFocus;
		this.selectionAnchorLayoutOffset = selectionAnchorLayoutOffset;
		this.selectionFocusLayoutOffset = selectionFocusLayoutOffset;
		this.selectionAnchorAffinity = selectionAnchorAffinity;
		this.selectionFocusAffinity = selectionFocusAffinity;
		this.scrollOffsetY = scrollOffsetY;
	}
}
