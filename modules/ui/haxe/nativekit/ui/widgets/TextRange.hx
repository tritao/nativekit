package nativekit.ui.widgets;

/** A half-open Unicode code-point range in an editor document. */
class TextRange {
	public final start:CodepointOffset;
	public final end:CodepointOffset;

	public function new(start:CodepointOffset, end:CodepointOffset) {
		this.start = start;
		this.end = end;
	}
}
