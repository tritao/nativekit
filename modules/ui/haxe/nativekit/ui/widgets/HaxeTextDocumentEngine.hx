package nativekit.ui.widgets;

import TextLayout;

/**
 * TextDocumentEngine implementation backed by the existing Haxe editor.
 *
 * This is deliberately an adapter. TextEditorState remains unchanged as the
 * first backend while platform and widget code can depend on the small core
 * contract. A Skribidi-backed implementation can replace this adapter later.
 */
class HaxeTextDocumentEngine implements TextDocumentEngine {
	public final state:TextEditorState;

	public function new(state:TextEditorState) {
		if (state == null)
			throw "A Haxe text document engine requires editor state";
		this.state = state;
	}

	public function applyEdit(transaction:EditTransaction):Void {
		state.applyTransaction(transaction);
	}

	public function text():String
		return state.layoutText();

	public function documentLength():CodepointOffset
		return state.documentLength();

	public function selection():SelectionState
		return new SelectionState(state.selectionStart, state.selectionEnd,
			state.selectionAnchor, state.selectionFocus,
			state.selectionAnchorAffinity, state.selectionFocusAffinity);

	public function composition():CompositionState
		return new CompositionState(state.queryComposition(), state.queryCompositionAttributes());

	public function commitComposition():Bool
		return state.commitComposition();

	public function cancelComposition():Bool
		return state.cancelComposition();

	public function undo():Bool
		return state.undo();

	public function redo():Bool
		return state.redo();

	public function layout(range:nativekit.ui.widgets.TextRange):LayoutResult {
		if (range == null)
			throw "A text layout range cannot be null";
		var count = state.documentLength();
		var start = clamp(range.start, 0, count);
		var end = clamp(range.end, 0, count);
		if (end < start) {
			var swap = start;
			start = end;
			end = swap;
		}
		var normalized = new nativekit.ui.widgets.TextRange(start, end);
		if (start == end)
			return new LayoutResult(normalized, [], state.layout.caret(new TextPosition(start, 0)));
		return new LayoutResult(normalized,
			state.layout.selectionRangeRects(new TextPosition(start, 0), new TextPosition(end, 0)));
	}

	public function hitTest(point:TextPoint):TextPosition {
		if (point == null)
			throw "A text hit-test point cannot be null";
		return state.hitTest(point.x, point.y);
	}

	static function clamp(value:CodepointOffset, low:Int, high:Int):CodepointOffset {
		var actual:Int = value;
		return actual < low ? low : (actual > high ? high : actual);
	}
}
