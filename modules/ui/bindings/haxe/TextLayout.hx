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
		if (width <= 0.0)
			throw "Text layout arguments are invalid";
		var actualText = text == null ? "" : text;
		var actualTextStyle = textStyle == null ? new TextStyle() : copyTextStyle(textStyle);
		var actualParagraphStyle = paragraphStyle == null ? new ParagraphStyle() : copyParagraphStyle(paragraphStyle);
		var made = NativeKitUI.nkui_text_layout_create_styled(fonts.nativeHandle(), actualText, width,
			nativeTextStyle(actualTextStyle), nativeParagraphStyle(actualParagraphStyle));
		UiResult.check(made.status, "textLayout.create");
		return new TextLayout(made.out_layout, actualText, width, actualTextStyle, actualParagraphStyle);
	}

	/** Creates a layout using the same semantic styles as LayoutNode. */
	public static function createStyled(fonts:FontCollection, text:String, width:Float,
			style:TextStyle, paragraph:ParagraphStyle):TextLayout
		return create(fonts, text, width, style, paragraph);

	/** Re-shapes this layout without replacing its native resource handle. */
	public function setText(value:String):Void {
		var actualText = value == null ? "" : value;
		UiResult.check(NativeKitUI.nkui_text_layout_set_text(nativeHandle(), actualText), "textLayout.setText");
		text = actualText;
	}

	/** Re-shapes this retained layout with new content, width, or semantic styles. */
	public function update(value:String, newWidth:Float, style:TextStyle,
			paragraph:ParagraphStyle):Void {
		if (newWidth <= 0.0 || style == null || paragraph == null)
			throw "Text layout update arguments are invalid";
		var actualText = value == null ? "" : value;
		var ownedTextStyle = copyTextStyle(style);
		var ownedParagraphStyle = copyParagraphStyle(paragraph);
		UiResult.check(NativeKitUI.nkui_text_layout_update(nativeHandle(), actualText, newWidth,
			nativeTextStyle(ownedTextStyle), nativeParagraphStyle(ownedParagraphStyle)), "textLayout.update");
		text = actualText;
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
		result.set_family(style.font);
		result.set_font_size(style.fontSize);
		result.set_letter_spacing(style.letterSpacing);
		return result;
	}

	static function nativeParagraphStyle(style:ParagraphStyle):nkui_paragraph_style {
		var result = new nkui_paragraph_style();
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

	/** Converts a shaped caret position into the editor's code-point insertion offset. */
	public function offsetFromPosition(position:TextPosition):Int {
		if (position == null)
			throw "Text position cannot be null";
		var result = NativeKitUI.nkui_text_layout_position_offset(nativeHandle(),
			nativePosition(position));
		UiResult.check(result.status, "textLayout.positionOffset");
		return result.out_offset;
	}

	/** Returns the code-point range for the word surrounding a hit-tested position. */
	public function wordRange(position:TextPosition):Array<Int> {
		if (position == null)
			throw "Word lookup requires a text position";
		var result = NativeKitUI.nkui_text_layout_word_range(nativeHandle(), nativePosition(position));
		UiResult.check(result.status, "textLayout.wordRange");
		return [result.out_start, result.out_end];
	}

	static function nativePosition(position:TextPosition):nkui_text_position {
		var result = new nkui_text_position();
		result.set_offset(position.offset);
		result.set_affinity(position.affinity);
		return result;
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

	/** Returns grapheme-aware visual rectangles for a code-point selection. */
	public function selectionRects(start:TextPosition, end:TextPosition):Array<Rect> {
		if (start == null || end == null)
			throw "Text selection endpoints cannot be null";
		var nativeStart = new nkui_text_position();
		nativeStart.set_offset(start.offset);
		nativeStart.set_affinity(start.affinity);
		var nativeEnd = new nkui_text_position();
		nativeEnd.set_offset(end.offset);
		nativeEnd.set_affinity(end.affinity);
		var result = NativeKitUI.nkui_text_layout_get_selection_rects(nativeHandle(), nativeStart, nativeEnd);
		UiResult.check(result.status, "textLayout.selectionRects");
		var bytes:haxe.io.Bytes = result.out_buffer;
		var recordBytes = 20;
		if (bytes.length % recordBytes != 0)
			throw "Text selection geometry contains a truncated rectangle";
		var rectangles:Array<Rect> = [];
		for (index in 0...Std.int(bytes.length / recordBytes)) {
			var offset = index * recordBytes;
			if (bytes.getInt32(offset) != recordBytes)
				throw "Text selection geometry returned an unsupported record size";
			rectangles.push(new Rect(readFloat(bytes, offset + 4), readFloat(bytes, offset + 8),
				readFloat(bytes, offset + 12), readFloat(bytes, offset + 16)));
		}
		return rectangles;
	}

	/** Returns the next grapheme boundary at or after a code-point offset. */
	public function nextGrapheme(offset:Int):Int {
		if (offset < 0)
			throw "Text position cannot be negative";
		var result = NativeKitUI.nkui_text_layout_next_grapheme(nativeHandle(), offset);
		UiResult.check(result.status, "textLayout.nextGrapheme");
		return result.out_offset;
	}

	/** Returns the previous grapheme boundary at or before a code-point offset. */
	public function previousGrapheme(offset:Int):Int {
		if (offset < 0)
			throw "Text position cannot be negative";
		var result = NativeKitUI.nkui_text_layout_previous_grapheme(nativeHandle(), offset);
		UiResult.check(result.status, "textLayout.previousGrapheme");
		return result.out_offset;
	}

	/** Aligns a code-point offset to its nearest grapheme boundary. */
	public function alignGrapheme(offset:Int):Int {
		if (offset < 0)
			throw "Text position cannot be negative";
		var result = NativeKitUI.nkui_text_layout_align_grapheme(nativeHandle(), offset);
		UiResult.check(result.status, "textLayout.alignGrapheme");
		return result.out_offset;
	}

	/** Returns Skribidi's word range containing a code-point offset. */
	public function wordRangeAt(offset:Int):TextRange {
		if (offset < 0)
			throw "Text position cannot be negative";
		var result = NativeKitUI.nkui_text_layout_word_range_at(nativeHandle(), offset);
		UiResult.check(result.status, "textLayout.wordRangeAt");
		return new TextRange(result.out_start, result.out_end);
	}

	/** Returns the visual line range containing a code-point offset. */
	public function lineRangeAt(offset:Int):TextRange {
		if (offset < 0)
			throw "Text position cannot be negative";
		var result = NativeKitUI.nkui_text_layout_line_range_at(nativeHandle(), offset);
		UiResult.check(result.status, "textLayout.lineRangeAt");
		return new TextRange(result.out_start, result.out_end);
	}

	/** Moves to a word boundary using Control-arrow or macOS Option-arrow conventions. */
	public function moveWord(offset:Int, direction:Int, macStyle:Bool = false):Int {
		if (offset < 0 || (direction != -1 && direction != 1))
			throw "Text word movement arguments are invalid";
		var behavior:NativeKitUI.TextNavigationBehavior = cast (macStyle ? 1 : 0);
		var result = NativeKitUI.nkui_text_layout_move_word(nativeHandle(), offset,
			direction, behavior);
		UiResult.check(result.status, "textLayout.moveWord");
		return result.out_offset;
	}

	/** Moves to a paragraph boundary using Control-arrow or macOS Option-arrow conventions. */
	public function moveParagraph(offset:Int, direction:Int, macStyle:Bool = false):Int {
		if (offset < 0 || (direction != -1 && direction != 1))
			throw "Text paragraph movement arguments are invalid";
		var behavior:NativeKitUI.TextNavigationBehavior = cast (macStyle ? 1 : 0);
		var result = NativeKitUI.nkui_text_layout_move_paragraph(nativeHandle(), offset,
			direction, behavior);
		UiResult.check(result.status, "textLayout.moveParagraph");
		return result.out_offset;
	}

	static inline function readFloat(bytes:haxe.io.Bytes, offset:Int):Float
		return floatFromBits(bytes.getInt32(offset));

	static function floatFromBits(bits:Int):Float {
		var sign = (bits >>> 31) == 0 ? 1.0 : -1.0;
		var exponent = (bits >>> 23) & 0xff;
		var fraction = bits & 0x7fffff;
		if (exponent == 255)
			throw "Text selection geometry contains a non-finite value";
		if (exponent == 0)
			return sign * fraction * Math.pow(2.0, -149.0);
		return sign * (0x800000 | fraction) * Math.pow(2.0, exponent - 150.0);
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

/** An ordered code-point range returned by a text layout query. */
class TextRange {
	public final start:Int;
	public final end:Int;

	public function new(start:Int, end:Int) {
		this.start = start;
		this.end = end;
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
