package nativekit.ui.widgets;

/**
 * Backend-neutral geometry returned for a document range.
 *
 * A collapsed range returns a caret when the backend can resolve one. A
 * non-collapsed range returns visual rectangles with absolute document
 * offsets, including wrapped and bidirectional fragments.
 */
class LayoutResult {
	public final range:TextRange;
	public final rects:Array<TextRangeRect>;
	public final caret:Null<TextCaret>;

	public function new(range:TextRange, ?rects:Array<TextRangeRect>,
			?caret:TextCaret) {
		this.range = range;
		this.rects = rects == null ? [] : rects.copy();
		this.caret = caret;
	}
}
