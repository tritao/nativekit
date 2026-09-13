package nativekit.ui.gestures;

import nativekit.ui.gestures.GestureKind;
import nativekit.ui.gestures.GestureRecognizer;

/** Recognizes two taps on the same target within a time and distance window. */
class DoubleTapRecognizer extends GestureRecognizer {
	public function new(?onDoubleTap:GestureEvent->Void, intervalSeconds:Float = 0.30,
			distanceTolerance:Float = 24.0)
		super(GestureKind.DoubleTap, distanceTolerance, intervalSeconds, onDoubleTap);
}
