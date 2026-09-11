import NativeKitUI;

/** A shaped text layout resource with a stable handle and mutable UTF-8 content. */
class TextLayout extends NativeKitUIResource {
	public var text(default, null):String;
	public final width:Float;
	public final fontSize:Float;

	private function new(value:nkui_resource, text:String, width:Float, fontSize:Float) {
		super(value);
		this.text = text;
		this.width = width;
		this.fontSize = fontSize;
	}

	public static function create(fonts:FontCollection, text:String, width:Float, fontSize:Float):TextLayout {
		if (text == null || width <= 0.0 || fontSize <= 0.0)
			throw "Text layout arguments are invalid";
		var made = NativeKitUI.nkui_text_layout_create(fonts.nativeHandle(), text, width, fontSize);
		UiResult.check(made.status, "textLayout.create");
		return new TextLayout(made.out_layout, text, width, fontSize);
	}

	/** Creates a layout from typed style objects so future text ABI growth does not add positional arguments. */
	public static function createStyled(fonts:FontCollection, text:String, style:TextStyle, paragraph:ParagraphStyle):TextLayout
		return create(fonts, text, paragraph.width, style.fontSize);

	/** Re-shapes this layout without replacing its native resource handle. */
	public function setText(value:String):Void {
		if (value == null)
			throw "Text layout text cannot be null";
		UiResult.check(NativeKitUI.nkui_text_layout_set_text(nativeHandle(), value), "textLayout.setText");
		text = value;
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
