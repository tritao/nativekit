package nativekit.ui.widgets;

/**
 * One atomic editor mutation.
 *
 * All positions are absolute Unicode code-point offsets in the document. A
 * platform adapter must convert its native offset representation before
 * creating this value. A null replacement text is treated as an empty string.
 */
class EditTransaction {
	public final replacementStart:Int;
	public final replacementEnd:Int;
	public final replacementText:Null<String>;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final hasComposition:Bool;
	public final compositionStart:Int;
	public final compositionEnd:Int;
	/** Caret affinity for the resulting selection focus. */
	public final selectionAffinity:Int;

	public function new(replacementStart:Int, replacementEnd:Int, replacementText:Null<String>,
			selectionStart:Int, selectionEnd:Int, ?hasComposition:Bool = false,
			?compositionStart:Int = -1, ?compositionEnd:Int = -1,
			?selectionAffinity:Int = 0) {
		this.replacementStart = replacementStart;
		this.replacementEnd = replacementEnd;
		this.replacementText = replacementText;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.hasComposition = hasComposition;
		this.compositionStart = compositionStart;
		this.compositionEnd = compositionEnd;
		this.selectionAffinity = selectionAffinity;
	}
}
