package nativekit.ui.widgets;

import FontCollection;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import ParagraphStyle;
import TextLayout;
import TextStyle;

/** Persistent editable text, selection and IME composition state for one widget ID. */
class TextEditorState {
	public var text(default, null):String;
	public var selectionStart(default, null):Int;
	public var selectionEnd(default, null):Int;
	public var compositionStart(default, null):Int;
	public var compositionEnd(default, null):Int;
	public var selectionAnchor(default, null):Int;
	public var selectionFocus(default, null):Int;
	public var focused:Bool;
	public var draggingSelection:Bool;
	public final layout:TextLayout;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	var lastLayoutWidth:Float;
	var lastLayoutText:String;
	var disposed:Bool;

	public function new(fonts:FontCollection, text:String, ?textStyle:TextStyle,
			?paragraphStyle:ParagraphStyle) {
		if (fonts == null || fonts.isDisposed())
			throw "Text editor requires a live font collection";
		this.text = text == null ? "" : text;
		this.textStyle = copyTextStyle(textStyle == null ? new TextStyle() : textStyle);
		this.paragraphStyle = copyParagraphStyle(paragraphStyle == null ? new ParagraphStyle() : paragraphStyle);
		var end = Utf8Text.length(this.text);
		selectionStart = end;
		selectionEnd = end;
		selectionAnchor = end;
		selectionFocus = end;
		compositionStart = -1;
		compositionEnd = -1;
		focused = false;
		draggingSelection = false;
		layout = TextLayout.create(fonts, layoutText(), 1.0, this.textStyle, this.paragraphStyle);
		lastLayoutWidth = 1.0;
		lastLayoutText = layoutText();
		disposed = false;
	}

	public function syncExternal(value:String):Bool {
		ensureLive();
		var next = value == null ? "" : value;
		if (next == text)
			return false;
		text = next;
		var caret = Utf8Text.length(text);
		selectionStart = caret;
		selectionEnd = caret;
		selectionAnchor = caret;
		selectionFocus = caret;
		clearComposition();
		layout.setText(layoutText());
		lastLayoutText = layoutText();
		return true;
	}

	public function updateLayout(width:Float):Void {
		ensureLive();
		var nextWidth = Math.max(1.0, width);
		var value = layoutText();
		if (nextWidth != lastLayoutWidth || value != lastLayoutText) {
			layout.update(value, nextWidth, textStyle, paragraphStyle);
			lastLayoutWidth = nextWidth;
			lastLayoutText = value;
		}
	}

	/** Inserts committed text over the current selection. */
	public function insert(value:String):Bool {
		if (value == null || value.length == 0)
			return false;
		return replace(selectionStart, selectionEnd, value);
	}

	/** Replaces a code-point range and places a collapsed caret after the insertion. */
	public function replace(start:Int, end:Int, value:String):Bool {
		ensureLive();
		var count = Utf8Text.length(text);
		var first = clamp(start, 0, count);
		var last = clamp(end, 0, count);
		if (last < first) {
			var swap = first;
			first = last;
			last = swap;
		}
		var next = Utf8Text.replace(text, first, last, value == null ? "" : value);
		if (next == text && first == last)
			return false;
		text = next;
		var caret = first + Utf8Text.length(value == null ? "" : value);
		selectionStart = caret;
		selectionEnd = caret;
		selectionAnchor = caret;
		selectionFocus = caret;
		clearComposition();
		layout.setText(layoutText());
		lastLayoutText = layoutText();
		return true;
	}

	/** Applies one transactional NativeKit composition/edit update. */
	public function applyTextEdit(edit:NativeKitTextEdit):Bool {
		ensureLive();
		if (edit == null)
			return false;
		var changed = false;
		var previousStart = selectionStart;
		var previousEnd = selectionEnd;
		var previousCompositionStart = compositionStart;
		var previousCompositionEnd = compositionEnd;
		switch (edit.action) {
			case TextEditAction.Compose | TextEditAction.Commit | TextEditAction.Delete:
				changed = replace(edit.replaceStart, edit.replaceEnd,
					edit.action == TextEditAction.Delete || edit.text == null ? "" : edit.text);
				setSelection(edit.selectionStart, edit.selectionEnd);
				if (edit.action == TextEditAction.Compose)
					setComposition(edit.compositionStart, edit.compositionEnd);
				else
					clearComposition();
			case TextEditAction.SetSelection:
				setSelection(edit.selectionStart, edit.selectionEnd);
				setComposition(edit.compositionStart, edit.compositionEnd);
			case TextEditAction.SetComposition:
				setComposition(edit.compositionStart, edit.compositionEnd);
			case TextEditAction.FinishComposition:
				clearComposition();
				setSelection(edit.selectionStart, edit.selectionEnd);
			case _:
				return false;
		}
		return changed || previousStart != selectionStart || previousEnd != selectionEnd ||
			previousCompositionStart != compositionStart || previousCompositionEnd != compositionEnd;
	}

	public function setSelection(start:Int, end:Int):Bool {
		ensureLive();
		var count = Utf8Text.length(text);
		var first = clamp(start, 0, count);
		var last = clamp(end, 0, count);
		if (first > last) {
			var swap = first;
			first = last;
			last = swap;
		}
		var changed = first != selectionStart || last != selectionEnd ||
			selectionAnchor != first || selectionFocus != last;
		selectionStart = first;
		selectionEnd = last;
		selectionAnchor = first;
		selectionFocus = last;
		return changed;
	}

	public function selectAll():Bool {
		var first = selectionStart != 0 || selectionEnd != Utf8Text.length(text) ||
			selectionAnchor != 0 || selectionFocus != Utf8Text.length(text);
		selectionAnchor = 0;
		selectionFocus = Utf8Text.length(text);
		selectionStart = 0;
		selectionEnd = selectionFocus;
		return first;
	}

	/** Places a caret and optionally extends the existing anchored selection. */
	public function placeCaret(offset:Int, extend:Bool):Bool {
		ensureLive();
		var next = clamp(layout.alignGrapheme(clamp(offset, 0, Utf8Text.length(text))),
			0, Utf8Text.length(text));
		if (!extend)
			selectionAnchor = next;
		else if (selectionStart == selectionEnd)
			selectionAnchor = selectionFocus;
		selectionFocus = next;
		var first = selectionAnchor < next ? selectionAnchor : next;
		var last = selectionAnchor > next ? selectionAnchor : next;
		var changed = first != selectionStart || last != selectionEnd;
		selectionStart = first;
		selectionEnd = last;
		return changed;
	}

	/** Moves by grapheme; a non-extended move collapses an existing selection first. */
	public function moveCaret(direction:Int, extend:Bool):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		var next:Int;
		if (!extend && selectionStart != selectionEnd)
			next = direction < 0 ? selectionStart : selectionEnd;
		else
			next = direction < 0 ? layout.previousGrapheme(selectionFocus) : layout.nextGrapheme(selectionFocus);
		if (!extend)
			selectionAnchor = next;
		else if (selectionStart == selectionEnd)
			selectionAnchor = selectionFocus;
		var first = extend ? (selectionAnchor < next ? selectionAnchor : next) : next;
		var last = extend ? (selectionAnchor > next ? selectionAnchor : next) : next;
		var changed = first != selectionStart || last != selectionEnd;
		selectionFocus = next;
		selectionStart = first;
		selectionEnd = last;
		return changed;
	}

	public function deleteBackward():Bool {
		if (selectionStart != selectionEnd)
			return replace(selectionStart, selectionEnd, "");
		if (selectionStart == 0)
			return false;
		var previous = layout.previousGrapheme(selectionStart);
		return replace(previous, selectionStart, "");
	}

	public function deleteForward():Bool {
		var length = Utf8Text.length(text);
		if (selectionStart != selectionEnd)
			return replace(selectionStart, selectionEnd, "");
		if (selectionEnd >= length)
			return false;
		var next = layout.nextGrapheme(selectionEnd);
		return replace(selectionEnd, next, "");
	}

	public function hitTest(x:Float, y:Float):Int {
		ensureLive();
		return layout.alignGrapheme(layout.hitTest(x, y).offset);
	}

	public function dispose():Void {
		if (disposed)
			return;
		layout.dispose();
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function setComposition(start:Int, end:Int):Bool {
		var count = Utf8Text.length(text);
		if (start < 0 || end < start || start > count || end > count) {
			return clearComposition();
		}
		if (start == compositionStart && end == compositionEnd)
			return false;
		compositionStart = start;
		compositionEnd = end;
		return true;
	}

	function clearComposition():Bool {
		if (compositionStart == -1 && compositionEnd == -1)
			return false;
		compositionStart = -1;
		compositionEnd = -1;
		return true;
	}

	function ensureLive():Void {
		if (disposed)
			throw "Text editor state has been disposed";
	}

	/** Native text services interpret Haxeon's null representation of empty strings as empty UTF-8. */
	public function layoutText():String
		return text == null ? "" : text;

	static function copyTextStyle(value:TextStyle):TextStyle
		return new TextStyle(value.fontSize, value.font, value.letterSpacing);

	static function copyParagraphStyle(value:ParagraphStyle):ParagraphStyle
		return new ParagraphStyle(value.wrap, value.alignment, value.lineHeight, value.direction);

	static inline function clamp(value:Int, minimum:Int, maximum:Int):Int
		return value < minimum ? minimum : value > maximum ? maximum : value;
}
