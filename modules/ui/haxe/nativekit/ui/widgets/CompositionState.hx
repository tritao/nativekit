package nativekit.ui.widgets;

/** Immutable composition state exposed by a TextDocumentEngine. */
class CompositionState {
	public final range:Null<TextRange>;
	public final attributes:Array<TextCompositionSpan>;

	public function new(range:Null<TextRange>, ?attributes:Array<TextCompositionSpan>) {
		this.range = range;
		this.attributes = attributes == null ? [] : attributes.copy();
	}

	public function isActive():Bool
		return range != null && range.end > range.start;
}
