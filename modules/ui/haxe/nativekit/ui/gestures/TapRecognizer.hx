package nativekit.ui.gestures;

import nativekit.ui.gestures.GestureKind;
import nativekit.ui.gestures.GestureRecognizer;

/** Recognizes a pointer release without a long press or drag. */
class TapRecognizer extends GestureRecognizer {
	public function new(?onTap:GestureEvent->Void, movementTolerance:Float = 8.0)
		super(GestureKind.Tap, movementTolerance, 0.0, onTap);
}
