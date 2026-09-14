package state;

/** Persistent gesture counters and animation values shown by the demos. */
class GestureDemoState {
	public var tweenValue:Float = 0.0;
	public var springValue:Float = 0.18;
	public var tapCount:Int = 0;
	public var doubleTapCount:Int = 0;
	public var longPressCount:Int = 0;
	public var dragCount:Int = 0;
	public var dragCardX:Float = 18.0;
	public var dragCardY:Float = 18.0;
	public var dragOriginX:Float = 18.0;
	public var dragOriginY:Float = 18.0;
	public var message:String = "Tap, double-tap, hold, or drag the card.";
}
