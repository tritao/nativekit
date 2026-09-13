package nativekit.ui.gestures;

import nativekit.ui.gestures.GestureKind;
import nativekit.ui.gestures.GestureRecognizer;

/** Recognizes a threshold-crossing drag and reports start, updates, and end. */
class DragRecognizer extends GestureRecognizer {
	public final onStart:GestureEvent->Void;
	public final onMove:GestureEvent->Void;
	public final onEnd:GestureEvent->Void;
	public final hasStart:Bool;
	public final hasMove:Bool;
	public final hasEnd:Bool;

	public function new(threshold:Float = 6.0, ?onStart:GestureEvent->Void,
			?onMove:GestureEvent->Void, ?onEnd:GestureEvent->Void) {
		super(GestureKind.Drag, threshold, 0.0, null);
		hasStart = onStart != null;
		hasMove = onMove != null;
		hasEnd = onEnd != null;
		this.onStart = onStart == null ? function(_) {} : onStart;
		this.onMove = onMove == null ? function(_) {} : onMove;
		this.onEnd = onEnd == null ? function(_) {} : onEnd;
	}
}
