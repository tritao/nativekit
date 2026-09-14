package nativekit.ui.widgets;

import FontCollection;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import ParagraphStyle;
import TextLayout;
import TextLayout.TextRange;
import TextStyle;

/** Persistent editable text, selection and IME composition state for one widget ID. */
class TextEditorState {
	static inline var DoubleClickInterval:Float = 0.4;
	static inline var ClickSlop:Float = 5.0;
	static inline var DragCharacters:Int = 0;
	static inline var DragWords:Int = 1;
	static inline var DragLines:Int = 2;

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
	var lastClickTime:Float;
	var lastClickX:Float;
	var lastClickY:Float;
	var clickCount:Int;
	var dragSelectionKind:Int;
	var dragInitialStart:Int;
	var dragInitialEnd:Int;
	var pointerDownX:Float;
	var pointerDownY:Float;
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
		lastClickTime = -1.0;
		lastClickX = 0.0;
		lastClickY = 0.0;
		clickCount = 0;
		dragSelectionKind = DragCharacters;
		dragInitialStart = end;
		dragInitialEnd = end;
		pointerDownX = 0.0;
		pointerDownY = 0.0;
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
		resetClickSequence();
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
		resetClickSequence();
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
		resetClickSequence();
		return moveFocusTo(next, extend);
	}

	/** Moves by Skribidi word boundaries; direction is -1 or +1. */
	public function moveCaretByWord(direction:Int, extend:Bool, macStyle:Bool = false):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		var next = !extend && selectionStart != selectionEnd
			? (direction < 0 ? selectionStart : selectionEnd)
			: layout.moveWord(selectionFocus, direction, macStyle);
		resetClickSequence();
		return moveFocusTo(next, extend);
	}

	/** Moves by paragraph for Control+Up/Down or macOS Option+Up/Down. */
	public function moveCaretByParagraph(direction:Int, extend:Bool,
			macStyle:Bool = false):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		var next = !extend && selectionStart != selectionEnd
			? (direction < 0 ? selectionStart : selectionEnd)
			: layout.moveParagraph(selectionFocus, direction, macStyle);
		resetClickSequence();
		return moveFocusTo(next, extend);
	}

	function moveFocusTo(next:Int, extend:Bool):Bool {
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

	/** Places a caret or selects a word/visual line on successive clicks. */
	public function pointerDown(offset:Int, x:Float, y:Float, extend:Bool, time:Float):Bool {
		ensureLive();
		draggingSelection = true;
		pointerDownX = x;
		pointerDownY = y;
		if (extend) {
			resetClickSequence();
			dragSelectionKind = DragCharacters;
			var changed = placeCaret(offset, true);
			dragInitialStart = selectionStart;
			dragInitialEnd = selectionEnd;
			return changed;
		}

		var dx = x - lastClickX;
		var dy = y - lastClickY;
		var isMultiClick = clickCount > 0 && time >= lastClickTime &&
			time - lastClickTime <= DoubleClickInterval &&
			dx * dx + dy * dy <= ClickSlop * ClickSlop;
		clickCount = isMultiClick ? (clickCount >= 3 ? 1 : clickCount + 1) : 1;
		lastClickTime = time;
		lastClickX = x;
		lastClickY = y;

		var changed = false;
		switch (clickCount) {
			case 2:
				var word = layout.wordRangeAt(offset);
				if (word.end > word.start) {
					dragSelectionKind = DragWords;
					changed = setSelection(word.start, word.end);
				} else {
					dragSelectionKind = DragCharacters;
					changed = placeCaret(offset, false);
				}
			case 3:
				var line = layout.lineRangeAt(offset);
				if (line.end > line.start) {
					dragSelectionKind = DragLines;
					changed = setSelection(line.start, line.end);
				} else {
					dragSelectionKind = DragCharacters;
					changed = placeCaret(offset, false);
				}
			case _:
				dragSelectionKind = DragCharacters;
				changed = placeCaret(offset, false);
		}
		dragInitialStart = selectionStart;
		dragInitialEnd = selectionEnd;
		return changed;
	}

	/** Extends a character, word, or line selection while dragging. */
	public function pointerMove(offset:Int, x:Float, y:Float):Bool {
		ensureLive();
		var dx = x - pointerDownX;
		var dy = y - pointerDownY;
		if (dx * dx + dy * dy > ClickSlop * ClickSlop)
			resetClickSequence();
		if (dragSelectionKind == DragWords)
			return extendGranularSelection(layout.wordRangeAt(offset));
		if (dragSelectionKind == DragLines)
			return extendGranularSelection(layout.lineRangeAt(offset));
		return placeCaret(offset, true);
	}

	public function pointerUp():Void {
		draggingSelection = false;
		dragSelectionKind = DragCharacters;
	}

	public function cancelPointer():Void {
		pointerUp();
		resetClickSequence();
	}

	function extendGranularSelection(range:TextRange):Bool {
		if (range == null || range.start == range.end)
			return false;
		var nextStart = range.start < dragInitialStart ? range.start : dragInitialStart;
		var nextEnd = range.end > dragInitialEnd ? range.end : dragInitialEnd;
		var nextAnchor = range.start < dragInitialStart ? dragInitialEnd : dragInitialStart;
		var nextFocus = range.start < dragInitialStart ? range.start :
			range.end > dragInitialEnd ? range.end : dragInitialEnd;
		var changed = nextStart != selectionStart || nextEnd != selectionEnd ||
			nextAnchor != selectionAnchor || nextFocus != selectionFocus;
		selectionStart = nextStart;
		selectionEnd = nextEnd;
		selectionAnchor = nextAnchor;
		selectionFocus = nextFocus;
		return changed;
	}

	function resetClickSequence():Void {
		clickCount = 0;
		lastClickTime = -1.0;
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
