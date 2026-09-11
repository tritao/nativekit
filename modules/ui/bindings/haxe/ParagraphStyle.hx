/** Typed paragraph layout inputs translated to the current text ABI. */
class ParagraphStyle {
	public final width:Float;

	public function new(width:Float) {
		if (width <= 0.0)
			throw "Paragraph width must be positive";
		this.width = width;
	}
}
