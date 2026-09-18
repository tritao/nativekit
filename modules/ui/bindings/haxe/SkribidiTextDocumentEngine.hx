import NativeKitUI;
import NativeKitUI.UiStatus;

/** TextDocumentEngine backed by Skribidi's native editor core. */
class SkribidiTextDocumentEngine extends NativeKitUIResource
        implements nativekit.ui.widgets.TextDocumentEngine {
    public final width:Float;
    public final textStyle:TextStyle;
    public final paragraphStyle:ParagraphStyle;
    var compositionMetadata:Array<nativekit.ui.widgets.TextCompositionSpan>;

    private function new(value:nkui_resource, width:Float, textStyle:TextStyle,
            paragraphStyle:ParagraphStyle) {
        super(value);
        this.width = width;
        this.textStyle = textStyle;
        this.paragraphStyle = paragraphStyle;
        this.compositionMetadata = [];
    }

    public static function create(fonts:FontCollection, text:String, width:Float,
            ?textStyle:TextStyle, ?paragraphStyle:ParagraphStyle):SkribidiTextDocumentEngine {
        if (fonts == null || fonts.isDisposed() || width <= 0.0)
            throw "Skribidi text document arguments are invalid";
        var actualTextStyle = textStyle == null ? new TextStyle() : copyTextStyle(textStyle);
        var actualParagraphStyle = paragraphStyle == null ? new ParagraphStyle() :
            copyParagraphStyle(paragraphStyle);
        var made = NativeKitUI.nkui_text_document_create(fonts.nativeHandle(),
            text == null ? "" : text, width, nativeTextStyle(actualTextStyle),
            nativeParagraphStyle(actualParagraphStyle));
        UiResult.check(made.status, "textDocument.create");
        return new SkribidiTextDocumentEngine(made.out_document, width, actualTextStyle,
            actualParagraphStyle);
    }

    public function applyEdit(transaction:nativekit.ui.widgets.EditTransaction):Void {
        if (transaction == null)
            throw "A text edit transaction cannot be null";
        UiResult.check(NativeKitUI.nkui_text_document_apply_edit(nativeHandle(),
            transaction.replacementStart, transaction.replacementEnd,
            transaction.replacementText == null ? "" : transaction.replacementText,
            transaction.selectionStart, transaction.selectionEnd, transaction.selectionAffinity,
            transaction.hasComposition ? 1 : 0, transaction.compositionStart,
            transaction.compositionEnd, Type.enumIndex(transaction.historyKind)), "textDocument.applyEdit");
        compositionMetadata = transaction.hasComposition && transaction.compositionAttributes != null ?
            transaction.compositionAttributes.copy() : [];
    }

    public function text():String {
        var result = NativeKitUI.nkui_text_document_get_text(nativeHandle());
        UiResult.check(result.status, "textDocument.text");
        var bytes:haxe.io.Bytes = result.out_buffer;
        return bytes.length == 0 ? "" : bytes.getString(0, bytes.length);
    }

    public function documentLength():nativekit.ui.widgets.CodepointOffset {
        var result = NativeKitUI.nkui_text_document_get_length(nativeHandle());
        UiResult.check(result.status, "textDocument.length");
        return result.out_length;
    }

    public function selection():nativekit.ui.widgets.SelectionState {
        var result = NativeKitUI.nkui_text_document_get_selection(nativeHandle());
        UiResult.check(result.status, "textDocument.selection");
        var value = result.out_selection;
        return new nativekit.ui.widgets.SelectionState(value.get_start(), value.get_end(),
            value.get_anchor(), value.get_focus(), value.get_anchor_affinity(),
            value.get_focus_affinity());
    }

    public function composition():nativekit.ui.widgets.CompositionState {
        var result = NativeKitUI.nkui_text_document_get_composition(nativeHandle());
        UiResult.check(result.status, "textDocument.composition");
        var value = result.out_composition;
        var range:Null<nativekit.ui.widgets.TextRange> = value.get_active() == 0 ? null :
            new nativekit.ui.widgets.TextRange(value.get_start(), value.get_end());
        return new nativekit.ui.widgets.CompositionState(range, compositionMetadata);
    }

    public function commitComposition():Bool {
        var status = NativeKitUI.nkui_text_document_commit_composition(nativeHandle());
        if (status == UiStatus.ErrorInvalidArgument)
            return false;
        UiResult.check(status, "textDocument.commitComposition");
        compositionMetadata = [];
        return true;
    }

    public function cancelComposition():Bool {
        var status = NativeKitUI.nkui_text_document_cancel_composition(nativeHandle());
        if (status == UiStatus.ErrorInvalidArgument)
            return false;
        UiResult.check(status, "textDocument.cancelComposition");
        compositionMetadata = [];
        return true;
    }

    public function undo():Bool {
        var status = NativeKitUI.nkui_text_document_undo(nativeHandle());
        if (status == UiStatus.ErrorInvalidArgument)
            return false;
        UiResult.check(status, "textDocument.undo");
        compositionMetadata = [];
        return true;
    }

    public function redo():Bool {
        var status = NativeKitUI.nkui_text_document_redo(nativeHandle());
        if (status == UiStatus.ErrorInvalidArgument)
            return false;
        UiResult.check(status, "textDocument.redo");
        compositionMetadata = [];
        return true;
    }

    public function layout(range:nativekit.ui.widgets.TextRange):nativekit.ui.widgets.LayoutResult {
        if (range == null)
            throw "A text layout range cannot be null";
        var count:Int = documentLength();
        var startOffset = clamp(range.start, 0, count);
        var endOffset = clamp(range.end, 0, count);
        if (endOffset < startOffset) {
            var swap = startOffset;
            startOffset = endOffset;
            endOffset = swap;
        }
        var normalized = new nativekit.ui.widgets.TextRange(startOffset, endOffset);
        var start = new TextPosition(startOffset, 0);
        var end = new TextPosition(endOffset, 0);
        var result = NativeKitUI.nkui_text_document_get_layout(nativeHandle(),
            nativePosition(start), nativePosition(end));
        UiResult.check(result.status, "textDocument.layout");
        var bytes:haxe.io.Bytes = result.out_buffer;
        var recordBytes = 20;
        if (bytes.length % recordBytes != 0)
            throw "Text document geometry contains a truncated rectangle";
        var rectangles:Array<TextRangeRect> = [];
        for (index in 0...Std.int(bytes.length / recordBytes)) {
            var offset = index * recordBytes;
            if (bytes.getInt32(offset) != recordBytes)
                throw "Text document geometry returned an unsupported record size";
            rectangles.push(new TextRangeRect(normalized.start, normalized.end,
                readFloat(bytes, offset + 4), readFloat(bytes, offset + 8),
                readFloat(bytes, offset + 12), readFloat(bytes, offset + 16)));
        }
        var caret:Null<TextCaret> = null;
        if (normalized.start == normalized.end)
            caret = caretAt(start);
        return new nativekit.ui.widgets.LayoutResult(normalized, rectangles, caret);
    }

    public function hitTest(point:nativekit.ui.widgets.TextPoint):TextPosition {
        if (point == null)
            throw "A text hit-test point cannot be null";
        var result = NativeKitUI.nkui_text_document_hit_test(nativeHandle(), point.x, point.y);
        UiResult.check(result.status, "textDocument.hitTest");
        return new TextPosition(result.out_position.get_offset(), result.out_position.get_affinity());
    }

    function caretAt(position:TextPosition):TextCaret {
        var result = NativeKitUI.nkui_text_document_caret(nativeHandle(), nativePosition(position));
        UiResult.check(result.status, "textDocument.caret");
        var value = result.out_caret;
        return new TextCaret(value.get_x(), value.get_y(), value.get_ascender(),
            value.get_descender(), value.get_slope(), value.get_direction());
    }

    static function nativePosition(position:TextPosition):nkui_text_position {
        var result = new nkui_text_position();
        result.set_offset(position.offset);
        result.set_affinity(position.affinity);
        return result;
    }

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

    static function copyTextStyle(style:TextStyle):TextStyle
        return new TextStyle(style.fontSize, style.font, style.letterSpacing);

    static function copyParagraphStyle(style:ParagraphStyle):ParagraphStyle
        return new ParagraphStyle(style.wrap, style.alignment, style.lineHeight, style.direction);

    static function clamp(value:Int, low:Int, high:Int):Int
        return value < low ? low : (value > high ? high : value);

    static function readFloat(bytes:haxe.io.Bytes, offset:Int):Float {
        var bits = bytes.getInt32(offset);
        var sign = (bits < 0) ? -1.0 : 1.0;
        var exponent = (bits >>> 23) & 0xff;
        var fraction = bits & 0x7fffff;
        if (exponent == 255)
            throw "Text document geometry contains a non-finite value";
        if (exponent == 0)
            return sign * fraction * Math.pow(2.0, -149.0);
        return sign * (0x800000 | fraction) * Math.pow(2.0, exponent - 150.0);
    }
}
