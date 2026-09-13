package nativekit.ui.semantics;

/** Selection/value details attached to a routed accessibility event. */
class AccessibilityActionData {
	public final action:Int;
	public final selectionStart:Int;
	public final selectionEnd:Int;
	public final granularity:Int;

	public function new(action:Int, selectionStart:Int, selectionEnd:Int, granularity:Int) {
		this.action = action;
		this.selectionStart = selectionStart;
		this.selectionEnd = selectionEnd;
		this.granularity = granularity;
	}
}
