package nativekit.ui.widgets;

/**
 * Generic metadata for one IME composition clause.
 *
 * Ranges use absolute Unicode code-point offsets, like EditTransaction. The
 * flags describe semantic state only; themes choose how selected and target
 * clauses are painted.
 */
class TextCompositionSpan {
	public final start:CodepointOffset;
	public final end:CodepointOffset;
	public final selected:Bool;
	public final target:Bool;

	public function new(start:CodepointOffset, end:CodepointOffset,
			selected:Bool = false, target:Bool = false) {
		this.start = start;
		this.end = end;
		this.selected = selected;
		this.target = target;
	}
}
