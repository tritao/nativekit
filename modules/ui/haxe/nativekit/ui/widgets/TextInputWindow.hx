package nativekit.ui.widgets;

/** A bounded UTF-8 window with absolute document code-point offsets. */
class TextInputWindow {
	public final text:String;
	public final start:CodepointOffset;
	public final end:CodepointOffset;

	public function new(text:String, start:CodepointOffset, end:CodepointOffset) {
		this.text = text == null ? "" : text;
		this.start = start;
		this.end = end;
	}
}
