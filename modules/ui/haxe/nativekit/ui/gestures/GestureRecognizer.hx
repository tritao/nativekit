package nativekit.ui.gestures;

/** Configurable callback and threshold shared by concrete gesture recognizers. */
class GestureRecognizer {
	public final kind:Int;
	public final threshold:Float;
	public final interval:Float;
	public final onRecognized:GestureEvent->Void;
	public final hasHandler:Bool;

	public function new(kind:Int, threshold:Float, interval:Float,
			onRecognized:Null<GestureEvent->Void>) {
		if (kind < GestureKind.Tap || kind > GestureKind.DragEnd || !finite(threshold) ||
			!finite(interval) || threshold < 0.0 || interval < 0.0 ||
			((kind == GestureKind.DoubleTap || kind == GestureKind.LongPress) && interval == 0.0))
			throw "Gesture thresholds and intervals must be finite and valid";
		this.kind = kind;
		this.threshold = threshold;
		this.interval = interval;
		hasHandler = onRecognized != null;
		this.onRecognized = onRecognized == null ? function(_) {} : onRecognized;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
