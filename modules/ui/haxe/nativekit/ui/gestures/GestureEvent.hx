package nativekit.ui.gestures;

import nativekit.ui.core.WidgetId;

/** Position and pointer data emitted by a Haxe gesture recognizer. */
class GestureEvent {
	public final kind:Int;
	public final target:WidgetId;
	public final pointerId:Int;
	public final startX:Float;
	public final startY:Float;
	public final x:Float;
	public final y:Float;
	public final deltaX:Float;
	public final deltaY:Float;
	public final elapsedSeconds:Float;

	public function new(kind:Int, target:WidgetId, pointerId:Int, startX:Float, startY:Float,
			x:Float, y:Float, deltaX:Float, deltaY:Float, elapsedSeconds:Float) {
		this.kind = kind;
		this.target = target;
		this.pointerId = pointerId;
		this.startX = startX;
		this.startY = startY;
		this.x = x;
		this.y = y;
		this.deltaX = deltaX;
		this.deltaY = deltaY;
		this.elapsedSeconds = elapsedSeconds;
	}
}
