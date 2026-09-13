package nativekit.ui.gestures;

import nativekit.ui.gestures.GestureKind;
import nativekit.ui.gestures.GestureRecognizer;

/** Recognizes a stationary pointer held for the configured duration. */
class LongPressRecognizer extends GestureRecognizer {
	public function new(?onLongPress:GestureEvent->Void, durationSeconds:Float = 0.50,
			movementTolerance:Float = 8.0)
		super(GestureKind.LongPress, movementTolerance, durationSeconds, onLongPress);
}
