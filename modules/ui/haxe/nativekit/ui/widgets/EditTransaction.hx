package nativekit.ui.widgets;

/**
 * One atomic editor mutation.
 *
 * All positions are absolute Unicode code-point offsets in the document. A
 * platform adapter must convert its native offset representation before
 * creating this value. A null replacement text is treated as an empty string.
 */
class EditTransaction {
	public final replacementStart:CodepointOffset;
	public final replacementEnd:CodepointOffset;
	public final replacementText:Null<String>;
	public final selectionStart:CodepointOffset;
	public final selectionEnd:CodepointOffset;
	public final hasComposition:Bool;
	public final compositionStart:CodepointOffset;
	public final compositionEnd:CodepointOffset;
	/** Caret affinity for the resulting selection focus. */
	public final selectionAffinity:Int;
	/** Optional generic clause metadata for the resulting composition. */
	public final compositionAttributes:Null<Array<TextCompositionSpan>>;

	public function new(replacementStart:CodepointOffset, replacementEnd:CodepointOffset,
			replacementText:Null<String>, selectionStart:CodepointOffset,
			selectionEnd:CodepointOffset, ?hasComposition:Bool = false,
			?compositionStart:CodepointOffset = -1, ?compositionEnd:CodepointOffset = -1,
			?selectionAffinity:Int = 0,
			?compositionAttributes:Array<TextCompositionSpan> = null) {
		this.replacementStart = replacementStart;
		this.replacementEnd = replacementEnd;
		this.replacementText = replacementText;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.hasComposition = hasComposition;
		this.compositionStart = compositionStart;
		this.compositionEnd = compositionEnd;
		this.selectionAffinity = selectionAffinity;
		this.compositionAttributes = compositionAttributes;
	}
}
