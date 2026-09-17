package nativekit.ui.widgets;

/** Immutable selection state exposed by a TextDocumentEngine. */
class SelectionState {
	public final start:CodepointOffset;
	public final end:CodepointOffset;
	public final anchor:CodepointOffset;
	public final focus:CodepointOffset;
	public final anchorAffinity:Int;
	public final focusAffinity:Int;

	public function new(start:CodepointOffset, end:CodepointOffset,
			anchor:CodepointOffset, focus:CodepointOffset,
			anchorAffinity:Int = 0, focusAffinity:Int = 0) {
		this.start = start;
		this.end = end;
		this.anchor = anchor;
		this.focus = focus;
		this.anchorAffinity = anchorAffinity;
		this.focusAffinity = focusAffinity;
	}

	public function isCollapsed():Bool
		return start == end;
}
