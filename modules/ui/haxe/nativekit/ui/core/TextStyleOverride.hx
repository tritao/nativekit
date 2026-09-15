package nativekit.ui.core;

import Color;
import FontFamily;

/**
 * Sparse typography changes applied to the current inherited text style.
 *
 * Null means inherit. A line height of zero is an explicit request for the
 * paragraph engine's automatic line height; it is therefore distinct from a
 * null lineHeight.
 */
class TextStyleOverride {
	public final font:Null<FontFamily>;
	public final fontSize:Null<Float>;
	public final letterSpacing:Null<Float>;
	public final wrap:Null<TextWrap>;
	public final alignment:Null<TextAlignment>;
	public final lineHeight:Null<Float>;
	public final direction:Null<TextDirection>;
	public final color:Null<Color>;

	public function new(?font:FontFamily, ?fontSize:Float, ?letterSpacing:Float,
			?wrap:TextWrap, ?alignment:TextAlignment, ?lineHeight:Float,
			?direction:TextDirection, ?color:Color) {
		if (fontSize != null && (!finite(fontSize) || fontSize <= 0.0))
			throw "Text style override font size is invalid";
		if (letterSpacing != null && !finite(letterSpacing))
			throw "Text style override letter spacing is invalid";
		if (lineHeight != null && (!finite(lineHeight) || lineHeight < 0.0))
			throw "Text style override line height is invalid";
		this.font = font;
		this.fontSize = fontSize;
		this.letterSpacing = letterSpacing;
		this.wrap = wrap;
		this.alignment = alignment;
		this.lineHeight = lineHeight;
		this.direction = direction;
		this.color = color;
	}

	/** Creates a text-only override without positional null placeholders. */
	public static function text(?fontSize:Float, ?letterSpacing:Float,
			?color:Color, ?font:FontFamily):TextStyleOverride
		return new TextStyleOverride(font, fontSize, letterSpacing, null, null, null, null, color);

	/** Creates a paragraph-only override without positional null placeholders. */
	public static function paragraph(?wrap:TextWrap, ?alignment:TextAlignment,
			?lineHeight:Float, ?direction:TextDirection):TextStyleOverride
		return new TextStyleOverride(null, null, null, wrap, alignment, lineHeight, direction);

	/** Creates a foreground-only override. */
	public static function foreground(color:Color):TextStyleOverride
		return new TextStyleOverride(null, null, null, null, null, null, null, color);

	/** Combines a parent override with a child override; child fields win. */
	public static function combine(parent:Null<TextStyleOverride>,
			child:Null<TextStyleOverride>):Null<TextStyleOverride> {
		if (parent == null)
			return child;
		if (child == null)
			return parent;
		return new TextStyleOverride(
			child.font == null ? parent.font : child.font,
			child.fontSize == null ? parent.fontSize : child.fontSize,
			child.letterSpacing == null ? parent.letterSpacing : child.letterSpacing,
			child.wrap == null ? parent.wrap : child.wrap,
			child.alignment == null ? parent.alignment : child.alignment,
			child.lineHeight == null ? parent.lineHeight : child.lineHeight,
			child.direction == null ? parent.direction : child.direction,
			child.color == null ? parent.color : child.color);
	}

	/** Turns a complete text style into a local override. */
	public static function fromTextStyle(style:Null<TextStyle>):Null<TextStyleOverride> {
		if (style == null)
			return null;
		return new TextStyleOverride(style.font, style.fontSize, style.letterSpacing);
	}

	/** Turns a complete paragraph style into a local override. */
	public static function fromParagraphStyle(style:Null<ParagraphStyle>):Null<TextStyleOverride> {
		if (style == null)
			return null;
		return new TextStyleOverride(null, null, null, style.wrap, style.alignment,
			style.lineHeight == null ? 0.0 : style.lineHeight, style.direction);
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
