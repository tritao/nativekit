import FontFamily;

/** Typed text styling inputs translated to the current text ABI. */
class TextStyle {
	public final fontSize:Float;
	public final family:FontFamily;

	public function new(fontSize:Float, family:FontFamily = FontFamily.Default) {
		if (fontSize <= 0.0)
			throw "Text font size must be positive";
		this.fontSize = fontSize;
		this.family = family;
	}
}
