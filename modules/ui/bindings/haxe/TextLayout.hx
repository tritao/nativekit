import NativeKitUI;

/** A shaped text layout resource with a stable handle and mutable UTF-8 content. */
class TextLayout extends NativeKitUIResource {
	public var text(default, null):String;
	public var width(default, null):Float;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;

	private function new(value:nkui_resource, text:String, width:Float, textStyle:TextStyle,
			paragraphStyle:ParagraphStyle) {
		super(value);
		this.text = text;
		this.width = width;
		this.textStyle = textStyle;
		this.paragraphStyle = paragraphStyle;
	}

	public static function create(fonts:FontCollection, text:String, width:Float,
			?textStyle:TextStyle, ?paragraphStyle:ParagraphStyle):TextLayout {
		if (text == null || width <= 0.0)
			throw "Text layout arguments are invalid";
		var actualTextStyle = textStyle == null ? new TextStyle() : copyTextStyle(textStyle);
		var actualParagraphStyle = paragraphStyle == null ? new ParagraphStyle() : copyParagraphStyle(paragraphStyle);
		var made = NativeKitUI.nkui_text_layout_create_styled(fonts.nativeHandle(), text, width,
			nativeTextStyle(actualTextStyle), nativeParagraphStyle(actualParagraphStyle));
		UiResult.check(made.status, "textLayout.create");
		return new TextLayout(made.out_layout, text, width, actualTextStyle, actualParagraphStyle);
	}

	/** Creates a layout using the same semantic styles as LayoutNode. */
	public static function createStyled(fonts:FontCollection, text:String, width:Float,
			style:TextStyle, paragraph:ParagraphStyle):TextLayout
		return create(fonts, text, width, style, paragraph);

	/** Re-shapes this layout without replacing its native resource handle. */
	public function setText(value:String):Void {
		if (value == null)
			throw "Text layout text cannot be null";
		UiResult.check(NativeKitUI.nkui_text_layout_set_text(nativeHandle(), value), "textLayout.setText");
		text = value;
	}

	/** Re-shapes this retained layout with new content, width, or semantic styles. */
	public function update(value:String, newWidth:Float, style:TextStyle,
			paragraph:ParagraphStyle):Void {
		if (value == null || newWidth <= 0.0 || style == null || paragraph == null)
			throw "Text layout update arguments are invalid";
		var ownedTextStyle = copyTextStyle(style);
		var ownedParagraphStyle = copyParagraphStyle(paragraph);
		UiResult.check(NativeKitUI.nkui_text_layout_update(nativeHandle(), value, newWidth,
			nativeTextStyle(ownedTextStyle), nativeParagraphStyle(ownedParagraphStyle)), "textLayout.update");
		text = value;
		width = newWidth;
		textStyle.font = ownedTextStyle.font;
		textStyle.fontSize = ownedTextStyle.fontSize;
		textStyle.letterSpacing = ownedTextStyle.letterSpacing;
		paragraphStyle.wrap = ownedParagraphStyle.wrap;
		paragraphStyle.alignment = ownedParagraphStyle.alignment;
		paragraphStyle.lineHeight = ownedParagraphStyle.lineHeight;
		paragraphStyle.direction = ownedParagraphStyle.direction;
	}

	static function copyTextStyle(style:TextStyle):TextStyle
		return new TextStyle(style.fontSize, style.font, style.letterSpacing);

	static function copyParagraphStyle(style:ParagraphStyle):ParagraphStyle
		return new ParagraphStyle(style.wrap, style.alignment, style.lineHeight, style.direction);

	static function nativeTextStyle(style:TextStyle):nkui_text_style {
		var result = new nkui_text_style();
		result.set_struct_size(nkui_text_style.size());
		result.set_family(style.font);
		result.set_font_size(style.fontSize);
		result.set_letter_spacing(style.letterSpacing);
		return result;
	}

	static function nativeParagraphStyle(style:ParagraphStyle):nkui_paragraph_style {
		var result = new nkui_paragraph_style();
		result.set_struct_size(nkui_paragraph_style.size());
		result.set_line_height(style.lineHeight == null ? 0.0 : style.lineHeight);
		result.set_wrap(style.wrap);
		result.set_alignment(style.alignment);
		result.set_direction(style.direction);
		return result;
	}

	public function measure():TextMetrics {
		var measured = NativeKitUI.nkui_text_layout_measure(nativeHandle());
		UiResult.check(measured.status, "textLayout.measure");
		return new TextMetrics(measured.out_metrics.get_x(), measured.out_metrics.get_y(), measured.out_metrics.get_width(),
			measured.out_metrics.get_height());
	}

	public function hitTest(x:Float, y:Float):TextPosition {
		var hit = NativeKitUI.nkui_text_layout_hit_test(nativeHandle(), x, y);
		UiResult.check(hit.status, "textLayout.hitTest");
		return new TextPosition(hit.out_position.get_offset(), hit.out_position.get_affinity());
	}

	public function caret(position:TextPosition):TextCaret {
		var caret = new nkui_text_position();
		caret.set_offset(position.offset);
		caret.set_affinity(position.affinity);
		var result = NativeKitUI.nkui_text_layout_caret(nativeHandle(), caret);
		UiResult.check(result.status, "textLayout.caret");
		return new TextCaret(result.out_caret.get_x(), result.out_caret.get_y(), result.out_caret.get_ascender(),
			result.out_caret.get_descender(), result.out_caret.get_slope(), result.out_caret.get_direction());
	}
}

/** Bounds returned by TextLayout.measure. */
class TextMetrics {
	public final x:Float;
	public final y:Float;
	public final width:Float;
	public final height:Float;

	public function new(x:Float, y:Float, width:Float, height:Float) {
		this.x = x;
		this.y = y;
		this.width = width;
		this.height = height;
	}
}

/** A Unicode code-point caret position. */
class TextPosition {
	public final offset:Int;
	public final affinity:Int;

	public function new(offset:Int, affinity:Int) {
		this.offset = offset;
		this.affinity = affinity;
	}
}

/** Visual geometry for a text caret. */
class TextCaret {
	public final x:Float;
	public final y:Float;
	public final ascender:Float;
	public final descender:Float;
	public final slope:Float;
	public final direction:Int;

	public function new(x:Float, y:Float, ascender:Float, descender:Float, slope:Float, direction:Int) {
		this.x = x;
		this.y = y;
		this.ascender = ascender;
		this.descender = descender;
		this.slope = slope;
		this.direction = direction;
	}
}
