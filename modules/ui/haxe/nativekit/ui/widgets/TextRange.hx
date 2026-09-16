package nativekit.ui.widgets;

/** A half-open Unicode code-point range in an editor document. */
class TextRange {
	public final start:Int;
	public final end:Int;

	public function new(start:Int, end:Int) {
		this.start = start;
		this.end = end;
	}
}
