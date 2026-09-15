package nativekit.ui.core;

import Color;
import nativekit.ui.theme.Theme;
import nativekit.ui.theme.TextRoleStyle;

/** Concrete typography snapshot emitted onto a text-capable render node. */
class ResolvedTextStyle {
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	public final textColor:Color;

	public function new(textStyle:TextStyle, paragraphStyle:ParagraphStyle, textColor:Color) {
		if (textStyle == null || paragraphStyle == null || textColor == null)
			throw "Resolved text styles require complete values";
		this.textStyle = copyTextStyle(textStyle);
		this.paragraphStyle = copyParagraphStyle(paragraphStyle);
		this.textColor = textColor;
	}

	public static function fromTheme(theme:Theme):ResolvedTextStyle {
		if (theme == null)
			throw "Resolved text styles require a theme";
		return fromRoleStyle(theme.body);
	}

	public static function fromRoleStyle(role:TextRoleStyle):ResolvedTextStyle {
		if (role == null)
			throw "Resolved text styles require a role";
		return new ResolvedTextStyle(role.textStyle, role.paragraphStyle, role.color);
	}

	/** Applies sparse local changes without mutating either input style. */
	public function merge(override:Null<TextStyleOverride>):ResolvedTextStyle {
		if (override == null)
			return new ResolvedTextStyle(textStyle, paragraphStyle, textColor);
		var nextText = new TextStyle(
			override.fontSize == null ? textStyle.fontSize : override.fontSize,
			override.font == null ? textStyle.font : override.font,
			override.letterSpacing == null ? textStyle.letterSpacing : override.letterSpacing);
		var nextParagraph = new ParagraphStyle(
			override.wrap == null ? paragraphStyle.wrap : override.wrap,
			override.alignment == null ? paragraphStyle.alignment : override.alignment,
			override.lineHeight == null ? paragraphStyle.lineHeight : override.lineHeight,
			override.direction == null ? paragraphStyle.direction : override.direction);
		return new ResolvedTextStyle(nextText, nextParagraph,
			override.color == null ? textColor : override.color);
	}

	/** Returns this snapshot with a state-dependent foreground color. */
	public function withTextColor(color:Color):ResolvedTextStyle {
		if (color == null)
			throw "Resolved text colors cannot be null";
		return new ResolvedTextStyle(textStyle, paragraphStyle, color);
	}

	static function copyTextStyle(value:TextStyle):TextStyle
		return new TextStyle(value.fontSize, value.font, value.letterSpacing);

	static function copyParagraphStyle(value:ParagraphStyle):ParagraphStyle
		return new ParagraphStyle(value.wrap, value.alignment, value.lineHeight, value.direction);
}
