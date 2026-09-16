package nativekit.ui.widgets;

import FontCollection;
import NativeKit.TextEditAction;
import NativeKitEventValue.NativeKitTextEdit;
import ParagraphStyle;
import Rect;
import TextLayout;
import TextStyle;

/** Persistent editable text, selection and IME composition state for one widget ID. */
class TextEditorState {
	static inline var caretBlinkHalfPeriod:Float = 0.5;

	public var text(default, null):String;
	public var selectionStart(default, null):CodepointOffset;
	public var selectionEnd(default, null):CodepointOffset;
	public var compositionStart(default, null):CodepointOffset;
	public var compositionEnd(default, null):CodepointOffset;
	public var selectionAnchor(default, null):CodepointOffset;
	public var selectionFocus(default, null):CodepointOffset;
	public var selectionAnchorLayoutOffset(default, null):CodepointOffset;
	public var selectionFocusLayoutOffset(default, null):CodepointOffset;
	public var selectionAnchorAffinity(default, null):Int;
	public var selectionFocusAffinity(default, null):Int;
	/** Vertical scroll offset in the editor's content coordinate space. */
	public var scrollOffsetY(default, null):Float;
	public var focused:Bool;
	public var draggingSelection:Bool;
	public final layout:TextLayout;
	public final textStyle:TextStyle;
	public final paragraphStyle:ParagraphStyle;
	var lastLayoutWidth:Float;
	var lastLayoutText:String;
	var lastPointerClickTime:Float;
	var lastPointerClickX:Float;
	var lastPointerClickY:Float;
	var lastPointerWordStart:Int;
	var lastPointerWordEnd:Int;
	var lastPointerClickCount:Int;
	var lastPointerClickArmed:Bool;
	var pointerClickPending:Bool;
	var viewportHeight:Float;
	var desiredVerticalX:Float;
	var hasDesiredVerticalX:Bool;
	var caretBlinkResetTime:Float;
	var compositionRestoreText:Null<String>;
	var compositionRestoreStart:CodepointOffset;
	var compositionRestoreEnd:CodepointOffset;
	var compositionRestoreSelectionStart:CodepointOffset;
	var compositionRestoreSelectionEnd:CodepointOffset;
	var hasCompositionRestoreState:Bool;
	/** Full-document map reused by every transaction and platform conversion. */
	var documentOffsetMap:TextOffsetMap;
	var activeParagraphOffsetMap:TextOffsetMap;
	var activeParagraphStart:Int;
	var activeParagraphEnd:Int;
	var disposed:Bool;

	public function new(fonts:FontCollection, text:String, ?textStyle:TextStyle,
			?paragraphStyle:ParagraphStyle) {
		if (fonts == null || fonts.isDisposed())
			throw "Text editor requires a live font collection";
		this.text = text == null ? "" : text;
		documentOffsetMap = new TextOffsetMap(this.text);
		activeParagraphOffsetMap = null;
		activeParagraphStart = -1;
		activeParagraphEnd = -1;
		this.textStyle = copyTextStyle(textStyle == null ? new TextStyle() : textStyle);
		this.paragraphStyle = copyParagraphStyle(paragraphStyle == null ? new ParagraphStyle() : paragraphStyle);
		var end = documentOffsetMap.codepointCount;
		selectionStart = end;
		selectionEnd = end;
		selectionAnchor = end;
		selectionFocus = end;
		selectionAnchorLayoutOffset = end;
		selectionFocusLayoutOffset = end;
		selectionAnchorAffinity = 0;
		selectionFocusAffinity = 0;
		scrollOffsetY = 0.0;
		compositionStart = -1;
		compositionEnd = -1;
		focused = false;
		draggingSelection = false;
		layout = TextLayout.create(fonts, layoutText(), 1.0, this.textStyle, this.paragraphStyle);
		lastLayoutWidth = 1.0;
		lastLayoutText = layoutText();
		lastPointerClickTime = -1.0;
		lastPointerClickX = 0.0;
		lastPointerClickY = 0.0;
		lastPointerWordStart = -1;
		lastPointerWordEnd = -1;
		lastPointerClickCount = 0;
		lastPointerClickArmed = false;
		pointerClickPending = false;
		viewportHeight = 0.0;
		desiredVerticalX = 0.0;
		hasDesiredVerticalX = false;
		caretBlinkResetTime = 0.0;
		compositionRestoreText = null;
		compositionRestoreStart = -1;
		compositionRestoreEnd = -1;
		compositionRestoreSelectionStart = -1;
		compositionRestoreSelectionEnd = -1;
		hasCompositionRestoreState = false;
		disposed = false;
	}

	public function syncExternal(value:String):Bool {
		ensureLive();
		var next = value == null ? "" : value;
		if (next == text)
			return false;
		cancelPointerClick();
		return replaceRange(0, documentLength(), next);
	}

	/** Returns the cached coordinate map for the complete document. */
	public function documentOffsets():TextOffsetMap {
		ensureLive();
		return documentOffsetMap;
	}

	/**
	 * Returns a cached map for the paragraph containing the active selection
	 * focus. The returned map is paragraph-local; use activeParagraphRange() to
	 * translate its code-point coordinates to document coordinates.
	 */
	public function activeParagraphOffsets():TextOffsetMap {
		ensureLive();
		var paragraph = documentOffsetMap.paragraphRangeAt(selectionFocus);
		var start:Int = paragraph.start;
		var end:Int = paragraph.end;
		if (activeParagraphOffsetMap == null || activeParagraphStart != start ||
			activeParagraphEnd != end) {
			activeParagraphOffsetMap = new TextOffsetMap(
				documentOffsetMap.sliceCodepoints(start, end));
			activeParagraphStart = start;
			activeParagraphEnd = end;
		}
		return activeParagraphOffsetMap;
	}

	/** Absolute document range represented by activeParagraphOffsets(). */
	public function activeParagraphRange():TextRange {
		ensureLive();
		var paragraph = documentOffsetMap.paragraphRangeAt(selectionFocus);
		activeParagraphOffsets();
		return new TextRange(paragraph.start, paragraph.end);
	}

	/** Cached Unicode scalar count used by editor coordinate calculations. */
	public function documentLength():Int
		return documentOffsetMap.codepointCount;

	public function updateLayout(width:Float):Void {
		ensureLive();
		var nextWidth = Math.max(1.0, width);
		var value = layoutText();
		if (nextWidth != lastLayoutWidth || value != lastLayoutText) {
			layout.update(value, nextWidth, textStyle, paragraphStyle);
			lastLayoutWidth = nextWidth;
			lastLayoutText = value;
		}
		clampScrollOffset();
	}

	/** Updates inherited typography without replacing the retained editor state. */
	public function updateStyle(nextTextStyle:TextStyle, nextParagraphStyle:ParagraphStyle):Bool {
		ensureLive();
		if (nextTextStyle == null || nextParagraphStyle == null)
			throw "Text editor styles cannot be null";
		var changed = textStyle.font != nextTextStyle.font ||
			textStyle.fontSize != nextTextStyle.fontSize ||
			textStyle.letterSpacing != nextTextStyle.letterSpacing ||
			paragraphStyle.wrap != nextParagraphStyle.wrap ||
			paragraphStyle.alignment != nextParagraphStyle.alignment ||
			paragraphStyle.lineHeight != nextParagraphStyle.lineHeight ||
			paragraphStyle.direction != nextParagraphStyle.direction;
		if (!changed)
			return false;
		textStyle.font = nextTextStyle.font;
		textStyle.fontSize = nextTextStyle.fontSize;
		textStyle.letterSpacing = nextTextStyle.letterSpacing;
		paragraphStyle.wrap = nextParagraphStyle.wrap;
		paragraphStyle.alignment = nextParagraphStyle.alignment;
		paragraphStyle.lineHeight = nextParagraphStyle.lineHeight;
		paragraphStyle.direction = nextParagraphStyle.direction;
		layout.update(layoutText(), Math.max(1.0, lastLayoutWidth), textStyle, paragraphStyle);
		lastLayoutText = layoutText();
		clampScrollOffset();
		return true;
	}

	/** Inserts committed text over the current selection. */
	public function insert(value:String):Bool {
		if (value == null || value.length == 0)
			return false;
		return replaceRange(selectionStart, selectionEnd, value);
	}

	/**
	 * Replaces a Unicode code-point range and places a collapsed caret after the
	 * inserted text. This is the convenience operation for a committed edit;
	 * all document mutation still goes through applyTransaction().
	 */
	public function replaceRange(start:Int, end:Int, value:Null<String>):Bool {
		ensureLive();
		var count = documentLength();
		var first = clamp(start, 0, count);
		var last = clamp(end, 0, count);
		if (last < first) {
			var swap = first;
			first = last;
			last = swap;
		}
		cancelPointerClick();
		var caret = first + TextOffsetMap.countCodepoints(value == null ? "" : value);
		return applyTransaction(new EditTransaction(first, last, value, caret, caret));
	}

	/** Compatibility alias for callers using the pre-transaction name. */
	public function replace(start:Int, end:Int, value:Null<String>):Bool
		return replaceRange(start, end, value);

	/** Applies one atomic editor transaction. */
	public function applyTransaction(transaction:EditTransaction):Bool {
		return applyTransactionInternal(transaction, true);
	}

	/** Applies one transactional NativeKit composition/edit update. */
	public function applyTextEdit(edit:NativeKitTextEdit):Bool {
		ensureLive();
		if (edit == null)
			return false;
		switch (edit.action) {
			case TextEditAction.Compose:
				return applyTransaction(new EditTransaction(edit.replaceStart, edit.replaceEnd,
					edit.text, edit.selectionStart, edit.selectionEnd, true,
					edit.compositionStart, edit.compositionEnd));
			case TextEditAction.Commit | TextEditAction.Delete:
				return applyTransaction(new EditTransaction(edit.replaceStart, edit.replaceEnd,
					edit.action == TextEditAction.Delete ? "" : edit.text,
					edit.selectionStart, edit.selectionEnd));
			case TextEditAction.SetSelection:
				return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
					edit.selectionStart, edit.selectionEnd, hasValidComposition(edit.compositionStart,
						edit.compositionEnd), edit.compositionStart, edit.compositionEnd), false);
			case TextEditAction.SetComposition:
				return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
					selectionStart, selectionEnd, hasValidComposition(edit.compositionStart,
						edit.compositionEnd), edit.compositionStart, edit.compositionEnd), false);
			case TextEditAction.FinishComposition:
				return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
					edit.selectionStart, edit.selectionEnd), false);
			case _:
				return false;
		}
		return false;
	}

	public function setSelection(start:Int, end:Int):Bool {
		ensureLive();
		return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
			start, end, compositionStart >= 0 && compositionEnd >= compositionStart,
			compositionStart, compositionEnd), false);
	}

	/** Sets the active composition range without replacing document text. */
	public function setComposition(start:Int, end:Int):Bool {
		var valid = hasValidComposition(start, end) && end <= documentLength();
		return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
			selectionStart, selectionEnd, valid, start, end), false);
	}

	/** Commits the current composition while keeping its document text. */
	public function commitComposition():Bool {
		return applyTransactionInternal(new EditTransaction(selectionStart, selectionStart, "",
			selectionStart, selectionEnd), false);
	}

	/** Cancels the current composition and restores its pre-composition state when available. */
	public function cancelComposition():Bool {
		ensureLive();
		if (compositionStart < 0 || compositionEnd < compositionStart)
			return false;
		if (!hasCompositionRestoreState)
			return commitComposition();
		return applyTransactionInternal(new EditTransaction(compositionStart, compositionEnd,
			compositionRestoreText, compositionRestoreSelectionStart,
			compositionRestoreSelectionEnd), false);
	}

	/** Returns the active composition range, or null when no composition is active. */
	public function queryComposition():Null<TextRange> {
		if (compositionStart < 0 || compositionEnd < compositionStart)
			return null;
		return new TextRange(compositionStart, compositionEnd);
	}

	/**
	 * Applies replacement, selection, and composition metadata as one state
	 * transition. The layout is updated only after the document mutation has
	 * been computed, so one transaction cannot publish a half-applied state.
	 */
	function applyTransactionInternal(transaction:EditTransaction,
			captureCompositionBaseline:Bool):Bool {
		ensureLive();
		if (transaction == null)
			return false;

		var current = layoutText();
		var count = documentLength();
		var first = clamp(transaction.replacementStart, 0, count);
		var last = clamp(transaction.replacementEnd, 0, count);
		if (last < first) {
			var swap = first;
			first = last;
			last = swap;
		}
		var replacement = transaction.replacementText == null ? "" : transaction.replacementText;
		var next = documentOffsetMap.replaceCodepoints(first, last, replacement);
		var nextCount = count - (last - first) + TextOffsetMap.countCodepoints(replacement);

		var nextSelectionStart = clamp(transaction.selectionStart, 0, nextCount);
		var nextSelectionEnd = clamp(transaction.selectionEnd, 0, nextCount);
		if (nextSelectionStart > nextSelectionEnd) {
			var selectionSwap = nextSelectionStart;
			nextSelectionStart = nextSelectionEnd;
			nextSelectionEnd = selectionSwap;
		}
		var nextHasComposition = transaction.hasComposition &&
			hasValidComposition(transaction.compositionStart, transaction.compositionEnd) &&
			transaction.compositionEnd <= nextCount;
		var nextCompositionStart = nextHasComposition ? transaction.compositionStart : -1;
		var nextCompositionEnd = nextHasComposition ? transaction.compositionEnd : -1;
		var hadComposition = compositionStart >= 0 && compositionEnd >= compositionStart;

		if (nextHasComposition && !hadComposition && captureCompositionBaseline) {
			compositionRestoreText = documentOffsetMap.sliceCodepoints(first, last);
			compositionRestoreStart = first;
			compositionRestoreEnd = last;
			compositionRestoreSelectionStart = selectionStart;
			compositionRestoreSelectionEnd = selectionEnd;
			hasCompositionRestoreState = true;
		} else if (!nextHasComposition) {
			clearCompositionBaseline();
		}

		var textChanged = next != current;
		var selectionChanged = selectionStart != nextSelectionStart ||
			selectionEnd != nextSelectionEnd || selectionAnchor != nextSelectionStart ||
			selectionFocus != nextSelectionEnd ||
			selectionAnchorLayoutOffset != nextSelectionStart ||
			selectionFocusLayoutOffset != nextSelectionEnd ||
			selectionAnchorAffinity != 0 ||
			selectionFocusAffinity != transaction.selectionAffinity;
		var compositionChanged = compositionStart != nextCompositionStart ||
			compositionEnd != nextCompositionEnd;
		if (!textChanged && !selectionChanged && !compositionChanged)
			return false;

		if (textChanged) {
			text = next;
			documentOffsetMap = new TextOffsetMap(next);
			activeParagraphOffsetMap = null;
			activeParagraphStart = -1;
			activeParagraphEnd = -1;
			layout.setText(layoutText());
			lastLayoutText = layoutText();
		}
		selectionStart = nextSelectionStart;
		selectionEnd = nextSelectionEnd;
		selectionAnchor = nextSelectionStart;
		selectionFocus = nextSelectionEnd;
		selectionAnchorLayoutOffset = nextSelectionStart;
		selectionFocusLayoutOffset = nextSelectionEnd;
		selectionAnchorAffinity = 0;
		selectionFocusAffinity = transaction.selectionAffinity;
		compositionStart = nextCompositionStart;
		compositionEnd = nextCompositionEnd;
		if (textChanged || selectionChanged)
			resetVerticalNavigation();
		return true;
	}

	public function selectAll():Bool {
		var first = selectionStart != 0 || selectionEnd != documentLength() ||
			selectionAnchor != 0 || selectionFocus != documentLength() ||
			selectionAnchorLayoutOffset != 0 ||
			selectionFocusLayoutOffset != documentLength() ||
			selectionAnchorAffinity != 0 || selectionFocusAffinity != 0;
		selectionAnchor = 0;
		selectionFocus = documentLength();
		selectionAnchorLayoutOffset = 0;
		selectionFocusLayoutOffset = selectionFocus;
		selectionStart = 0;
		selectionEnd = selectionFocus;
		selectionAnchorAffinity = 0;
		selectionFocusAffinity = 0;
		resetVerticalNavigation();
		return first;
	}

	/** Places a caret and optionally extends the existing anchored selection. */
	public function placeCaret(offset:Int, extend:Bool, affinity:Int = 0):Bool {
		ensureLive();
		resetVerticalNavigation();
		var next = clamp(layout.alignGrapheme(clamp(offset, 0, documentLength())),
			0, documentLength());
		var previousFocusLayoutOffset = selectionFocusLayoutOffset;
		var previousAnchorLayoutOffset = selectionAnchorLayoutOffset;
		if (!extend) {
			selectionAnchor = next;
			selectionAnchorLayoutOffset = next;
			selectionAnchorAffinity = affinity;
		} else if (selectionStart == selectionEnd) {
			selectionAnchor = selectionFocus;
			selectionAnchorLayoutOffset = previousFocusLayoutOffset;
			selectionAnchorAffinity = selectionFocusAffinity;
		}
		selectionFocus = next;
		selectionFocusLayoutOffset = next;
		var changed = selectionAnchorLayoutOffset != previousAnchorLayoutOffset ||
			selectionFocusLayoutOffset != previousFocusLayoutOffset ||
			selectionAnchorAffinity != affinity || selectionFocusAffinity != affinity;
		selectionFocusAffinity = affinity;
		var first = selectionAnchor < next ? selectionAnchor : next;
		var last = selectionAnchor > next ? selectionAnchor : next;
		changed = changed || first != selectionStart || last != selectionEnd;
		selectionStart = first;
		selectionEnd = last;
		return changed;
	}

	/** Moves by grapheme; a non-extended move collapses an existing selection first. */
	public function moveCaret(direction:Int, extend:Bool):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		resetVerticalNavigation();
		var next:Int;
		if (!extend && selectionStart != selectionEnd)
			next = direction < 0 ? selectionStart : selectionEnd;
		else
			next = direction < 0 ? layout.previousGrapheme(selectionFocus) : layout.nextGrapheme(selectionFocus);
		cancelPointerClick();
		return moveFocusTo(next, extend);
	}

	/** Moves by Skribidi word boundaries; direction is -1 or +1. */
	public function moveCaretByWord(direction:Int, extend:Bool, macStyle:Bool = false):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		resetVerticalNavigation();
		var next = !extend && selectionStart != selectionEnd
			? (direction < 0 ? selectionStart : selectionEnd)
			: layout.moveWord(selectionFocus, direction, macStyle);
		cancelPointerClick();
		return moveFocusTo(next, extend);
	}

	/** Moves by paragraph for Control+Up/Down or macOS Option+Up/Down. */
	public function moveCaretByParagraph(direction:Int, extend:Bool,
			macStyle:Bool = false):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		resetVerticalNavigation();
		var next = !extend && selectionStart != selectionEnd
			? (direction < 0 ? selectionStart : selectionEnd)
			: layout.moveParagraph(selectionFocus, direction, macStyle);
		cancelPointerClick();
		return moveFocusTo(next, extend);
	}

	/** Moves to the nearest visual line while preserving the requested x column. */
	public function moveCaretVertically(direction:Int, extend:Bool):Bool {
		ensureLive();
		if (direction == 0)
			return false;
		if (!extend && selectionStart != selectionEnd) {
			resetVerticalNavigation();
			return moveFocusTo(direction < 0 ? selectionStart : selectionEnd, false);
		}

		var current = focusPosition();
		var currentCaret = layout.caret(current);
		if (!hasDesiredVerticalX) {
			desiredVerticalX = currentCaret.x;
			hasDesiredVerticalX = true;
		}
		var lineStep = paragraphStyle.lineHeight == null ?
			absolute(currentCaret.descender - currentCaret.ascender) : paragraphStyle.lineHeight;
		lineStep = Math.max(1.0, lineStep);

		var candidate:TextPosition = null;
		var candidateCaret:TextCaret = null;
		// Hit testing at the next baseline handles wrapped visual lines and keeps
		// the shaping engine's bidi affinity. A small widening probe is useful at
		// line boundaries where hit testing deliberately favors the current line.
		for (probe in 0...5) {
			var distance = lineStep * (1.0 + probe * 0.25);
			var hit = layout.hitTest(desiredVerticalX, currentCaret.y + direction * distance);
			var hitCaret = layout.caret(hit);
			if ((direction < 0 && hitCaret.y < currentCaret.y - 0.01) ||
				(direction > 0 && hitCaret.y > currentCaret.y + 0.01)) {
				candidate = hit;
				candidateCaret = hitCaret;
				break;
			}
		}
		if (candidate == null || candidateCaret == null)
			return false;

		var currentRange = layout.lineRangeAt(current.offset);
		var candidateRange = layout.lineRangeAt(candidate.offset);
		if (currentRange.start == candidateRange.start && currentRange.end == candidateRange.end)
			return false;
		cancelPointerClick();
		var changed = placeCaretAt(candidate, extend);
		// placeCaretAt resets navigation state as a normal horizontal/pointer
		// move would. Restore the column for a continued up/down sequence.
		hasDesiredVerticalX = true;
		return changed;
	}

	/** Moves to the start or end of the current visual line. */
	public function moveCaretToLineBoundary(endOfLine:Bool, extend:Bool):Bool {
		ensureLive();
		var range = layout.lineRangeAt(selectionFocusLayoutOffset);
		var target = endOfLine ? trimLineBreak(range.end, range.start) : range.start;
		resetVerticalNavigation();
		return placeCaret(target, extend);
	}

	/** Adjusts internal scrolling so the active caret or selection remains in the viewport. */
	public function ensureCaretVisible(height:Float):Bool {
		ensureLive();
		if (!Math.isFinite(height) || height <= 0.0)
			return false;
		viewportHeight = height;
		var metrics = layout.measure();
		var contentHeight = Math.max(height, metrics.height);
		var caret = layout.caret(focusPosition());
		var top = caret.y + Math.min(caret.ascender, caret.descender);
		var bottom = caret.y + Math.max(caret.ascender, caret.descender);
		if (selectionStart != selectionEnd) {
			var selectionTop = top;
			var selectionBottom = bottom;
			for (rect in layout.selectionRects(anchorPosition(), focusPosition())) {
				selectionTop = Math.min(selectionTop, rect.y);
				selectionBottom = Math.max(selectionBottom, rect.y + rect.height);
			}
			// A selection can be taller than the viewport. In that case both
			// endpoints cannot be shown simultaneously, so keep the active focus
			// visible instead of jumping away from it.
			if (selectionBottom - selectionTop <= height) {
				top = selectionTop;
				bottom = selectionBottom;
			}
		}
		var next = scrollOffsetY;
		if (top < next)
			next = top;
		else if (bottom > next + height)
			next = bottom - height;
		var maximum = Math.max(0.0, contentHeight - height);
		next = clampFloat(next, 0.0, maximum);
		if (next == scrollOffsetY)
			return false;
		scrollOffsetY = next;
		return true;
	}

	function moveFocusTo(next:Int, extend:Bool):Bool {
		var previousFocusLayoutOffset = selectionFocusLayoutOffset;
		var wasCollapsed = selectionStart == selectionEnd;
		if (!extend) {
			selectionAnchor = next;
			selectionAnchorLayoutOffset = next;
		} else if (wasCollapsed) {
			selectionAnchor = selectionFocus;
			selectionAnchorLayoutOffset = previousFocusLayoutOffset;
		}
		var first = extend ? (selectionAnchor < next ? selectionAnchor : next) : next;
		var last = extend ? (selectionAnchor > next ? selectionAnchor : next) : next;
		var changed = first != selectionStart || last != selectionEnd ||
			selectionFocusLayoutOffset != next || selectionFocusAffinity != 0;
		selectionFocus = next;
		selectionFocusLayoutOffset = next;
		selectionFocusAffinity = 0;
		if (!extend) {
			selectionAnchorAffinity = 0;
		}
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
		var length = documentLength();
		if (selectionStart != selectionEnd)
			return replace(selectionStart, selectionEnd, "");
		if (selectionEnd >= length)
			return false;
		var next = layout.nextGrapheme(selectionEnd);
		return replace(selectionEnd, next, "");
	}

	public function hitTest(x:Float, y:Float):TextPosition {
		ensureLive();
		var hit = layout.hitTest(x, y);
		return new TextPosition(layout.alignGrapheme(hit.offset), hit.affinity);
	}

	/** Places the caret from a shaping-engine hit while keeping its visual affinity. */
	public function placeCaretAt(position:TextPosition, extend:Bool):Bool {
		ensureLive();
		resetVerticalNavigation();
		if (position == null)
			return false;
		var previousFocusLayoutOffset = selectionFocusLayoutOffset;
		var previousFocusAffinity = selectionFocusAffinity;
		var wasCollapsed = selectionStart == selectionEnd;
		var next = layout.offsetFromPosition(position);
		var changed = placeCaret(next, extend, position.affinity);
		selectionFocusLayoutOffset = position.offset;
		if (!extend)
			selectionAnchorLayoutOffset = position.offset;
		else if (wasCollapsed) {
			selectionAnchorLayoutOffset = previousFocusLayoutOffset;
			selectionAnchorAffinity = previousFocusAffinity;
		}
		return changed || previousFocusLayoutOffset != selectionFocusLayoutOffset;
	}

	public function focusPosition():TextPosition
		return new TextPosition(selectionFocusLayoutOffset, selectionFocusAffinity);

	public function anchorPosition():TextPosition
		return new TextPosition(selectionAnchorLayoutOffset, selectionAnchorAffinity);

	/** Restarts the caret blink after keyboard, pointer, or text-edit activity. */
	public function resetCaretBlink(timeSeconds:Float):Void {
		if (finite(timeSeconds))
			caretBlinkResetTime = Math.max(0.0, timeSeconds);
	}

	/** Returns whether the focused caret should be painted at the given UI time. */
	public function isCaretVisible(timeSeconds:Float):Bool {
		if (!focused || !finite(timeSeconds))
			return false;
		var elapsed = Math.max(0.0, timeSeconds - caretBlinkResetTime);
		return Std.int(elapsed / caretBlinkHalfPeriod) % 2 == 0;
	}

	/** Returns the shaped rectangles used to paint the active IME preedit underline. */
	public function compositionRects():Array<Rect> {
		if (compositionStart < 0 || compositionEnd <= compositionStart)
			return [];
		return layout.selectionRects(new TextPosition(compositionStart, 0),
			new TextPosition(compositionEnd, 0));
	}

	/** Selects the word under a pointer position using the shaped text engine's boundaries. */
	public function selectWordAt(position:TextPosition):Bool {
		ensureLive();
		if (position == null || documentLength() == 0)
			return false;
		var range = layout.wordRange(position);
		return setSelection(range[0], range[1]);
	}

	/** Selects the visual line under a pointer position. */
	public function selectLineAt(position:TextPosition):Bool {
		ensureLive();
		if (position == null || documentLength() == 0)
			return false;
		var range = layout.lineRangeAt(position.offset);
		return setSelection(range.start, range.end);
	}

	/** Returns 1 for a single click, 2 for a double click, and 3 for a triple click. */
	public function registerPointerClick(position:TextPosition, timestamp:Float,
			x:Float, y:Float):Int {
		ensureLive();
		if (position == null || !finite(timestamp) || !finite(x) || !finite(y)) {
			cancelPointerClick();
			return 1;
		}
		var range = layout.wordRange(position);
		var dx = x - lastPointerClickX;
		var dy = y - lastPointerClickY;
		var sameSequence = lastPointerClickTime >= 0.0 && timestamp >= lastPointerClickTime &&
			timestamp - lastPointerClickTime <= 0.5 && range[0] == lastPointerWordStart &&
			range[1] == lastPointerWordEnd && dx * dx + dy * dy <= 36.0 && lastPointerClickArmed;
		lastPointerClickCount = sameSequence
			? (lastPointerClickCount >= 3 ? 1 : lastPointerClickCount + 1)
			: 1;
		lastPointerClickTime = timestamp;
		lastPointerClickX = x;
		lastPointerClickY = y;
		lastPointerWordStart = range[0];
		lastPointerWordEnd = range[1];
		lastPointerClickArmed = false;
		pointerClickPending = true;
		return lastPointerClickCount;
	}

	/** Arms a click for a possible double click only after a nearby pointer release. */
	public function completePointerClick(x:Float, y:Float):Void {
		if (!pointerClickPending || !finite(x) || !finite(y)) {
			cancelPointerClick();
			return;
		}
		var dx = x - lastPointerClickX;
		var dy = y - lastPointerClickY;
		if (dx * dx + dy * dy > 36.0) {
			cancelPointerClick();
			return;
		}
		pointerClickPending = false;
		lastPointerClickArmed = true;
	}

	/** Cancels click recognition once movement exceeds the platform-independent slop. */
	public function cancelPointerClickIfMoved(x:Float, y:Float):Void {
		if (!pointerClickPending)
			return;
		var dx = x - lastPointerClickX;
		var dy = y - lastPointerClickY;
		if (dx * dx + dy * dy > 36.0)
			cancelPointerClick();
	}

	public function cancelPointerClick():Void {
		lastPointerClickTime = -1.0;
		lastPointerClickX = 0.0;
		lastPointerClickY = 0.0;
		lastPointerWordStart = -1;
		lastPointerWordEnd = -1;
		lastPointerClickCount = 0;
		lastPointerClickArmed = false;
		pointerClickPending = false;
	}

	public function dispose():Void {
		if (disposed)
			return;
		layout.dispose();
		documentOffsetMap = null;
		activeParagraphOffsetMap = null;
		disposed = true;
	}

	public function isDisposed():Bool
		return disposed;

	function clearCompositionBaseline():Void {
		compositionRestoreText = null;
		compositionRestoreStart = -1;
		compositionRestoreEnd = -1;
		compositionRestoreSelectionStart = -1;
		compositionRestoreSelectionEnd = -1;
		hasCompositionRestoreState = false;
	}

	function trimLineBreak(offset:Int, lineStart:Int):Int {
		var result = offset;
		while (result > lineStart) {
			var value = documentOffsetMap.sliceCodepoints(result - 1, result);
			if (value != "\n" && value != "\r")
				break;
			result--;
		}
		return result;
	}

	function resetVerticalNavigation():Void
		hasDesiredVerticalX = false;

	function clampScrollOffset():Void {
		if (viewportHeight <= 0.0)
			return;
		var maximum = Math.max(0.0, layout.measure().height - viewportHeight);
		scrollOffsetY = clampFloat(scrollOffsetY, 0.0, maximum);
	}

	function ensureLive():Void {
		if (disposed)
			throw "Text editor state has been disposed";
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	/** Native text services interpret Haxeon's null representation of empty strings as empty UTF-8. */
	public function layoutText():String
		return text == null ? "" : text;

	static function copyTextStyle(value:TextStyle):TextStyle
		return new TextStyle(value.fontSize, value.font, value.letterSpacing);

	static function copyParagraphStyle(value:ParagraphStyle):ParagraphStyle
		return new ParagraphStyle(value.wrap, value.alignment, value.lineHeight, value.direction);

	static inline function clamp(value:Int, minimum:Int, maximum:Int):Int
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static inline function hasValidComposition(start:Int, end:Int):Bool
		return start >= 0 && end >= start;

	static inline function absolute(value:Float):Float
		return value < 0.0 ? -value : value;

	static inline function clampFloat(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;
}
