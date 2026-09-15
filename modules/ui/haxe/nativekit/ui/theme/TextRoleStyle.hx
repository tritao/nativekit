package nativekit.ui.theme;

import Color;
import ParagraphStyle;
import TextStyle;
import nativekit.ui.core.TextStyleOverride;

/** Complete concrete typography and color definition for one semantic role. */
class TextRoleStyle {
	public var textStyle:TextStyle;
	public var paragraphStyle:ParagraphStyle;
	public var color:Color;

	public function new(textStyle:TextStyle, paragraphStyle:ParagraphStyle, color:Color) {
		if (textStyle == null || paragraphStyle == null || color == null)
			throw "Text role styles require complete values";
		this.textStyle = textStyle;
		this.paragraphStyle = paragraphStyle;
		this.color = color;
	}

	/** Converts this complete role into a sparse style usable in a nested scope. */
	public function toOverride():TextStyleOverride
		return new TextStyleOverride(textStyle.font, textStyle.fontSize,
			textStyle.letterSpacing, paragraphStyle.wrap, paragraphStyle.alignment,
			paragraphStyle.lineHeight == null ? 0.0 : paragraphStyle.lineHeight,
			paragraphStyle.direction, color);
}
