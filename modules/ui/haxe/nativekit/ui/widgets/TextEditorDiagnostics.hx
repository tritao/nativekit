package nativekit.ui.widgets;

import Rect;

/** Frame-local text and IME state exposed by an active TextField. */
class TextEditorDiagnostics {
	public final key:String;
	public final label:String;
	public final focused:Bool;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final caretOffset:Int;
	public final compositionStart:Int;
	public final compositionEnd:Int;
	public final compositionText:String;
	public final caretRect:Null<Rect>;
	public final platformSupported:Bool;
	public final platformActive:Bool;

	public function new(key:String, label:String, focused:Bool, selectionStart:Int,
			selectionEnd:Int, caretOffset:Int, compositionStart:Int, compositionEnd:Int,
			compositionText:String, caretRect:Null<Rect>, platformSupported:Bool,
			platformActive:Bool) {
		this.key = key;
		this.label = label;
		this.focused = focused;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.caretOffset = caretOffset;
		this.compositionStart = compositionStart;
		this.compositionEnd = compositionEnd;
		this.compositionText = compositionText == null ? "" : compositionText;
		this.caretRect = caretRect;
		this.platformSupported = platformSupported;
		this.platformActive = platformActive;
	}
}
