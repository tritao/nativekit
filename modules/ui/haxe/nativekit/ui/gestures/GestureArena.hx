package nativekit.ui.gestures;

import nativekit.ui.core.UiEvent;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.WidgetId;
import nativekit.ui.gestures.DragRecognizer;
import nativekit.ui.gestures.GestureEvent;
import nativekit.ui.gestures.GestureKind;
import nativekit.ui.gestures.GestureRecognizer;

private class GestureSequence {
	public final target:WidgetId;
	public final pointerId:Int;
	public final startX:Float;
	public final startY:Float;
	public final recognizers:Array<GestureRecognizer>;
	public var x:Float;
	public var y:Float;
	public var elapsed:Float;
	public var moved:Bool;
	public var longPressed:Bool;
	public var dragging:Bool;

	public function new(target:WidgetId, event:UiEvent, recognizers:Array<GestureRecognizer>) {
		this.target = target;
		pointerId = event.pointerId;
		startX = x = event.x;
		startY = y = event.y;
		this.recognizers = recognizers.copy();
		elapsed = 0.0;
		moved = false;
		longPressed = false;
		dragging = false;
	}
}

private class LastTap {
	public final target:WidgetId;
	public final x:Float;
	public final y:Float;
	public final time:Float;

	public function new(target:WidgetId, x:Float, y:Float, time:Float) {
		this.target = target;
		this.x = x;
		this.y = y;
		this.time = time;
	}
}

/** Frame-ticked Haxe gesture arena shared by rebuilt GestureDetector views. */
class GestureArena {
	final active:Map<Int, GestureSequence>;
	final lastTaps:Map<Int, LastTap>;
	var elapsedSeconds:Float;
	public var revision(default, null):Int;

	public function new() {
		active = new Map();
		lastTaps = new Map();
		elapsedSeconds = 0.0;
		revision = 0;
	}

	/** Monotonic UI time accumulated from submitted frame deltas. */
	public function timeSeconds():Float
		return elapsedSeconds;

	public function pointerDown(target:WidgetId, event:UiEvent,
			recognizers:Array<GestureRecognizer>):Void {
		if (target == null || event == null || recognizers == null)
			return;
		active.set(event.pointerId, new GestureSequence(target, event, recognizers));
		revision++;
	}

	/** Drops in-flight recognizers whose keyed detector left the rebuilt tree. */
	public function setRoot(root:Null<RenderNode>):Void {
		var stale:Array<Int> = [];
		for (pointerId in active.keys()) {
			var sequence = active.get(pointerId);
			if (root == null || sequence == null || root.find(sequence.target) == null)
				stale.push(pointerId);
		}
		for (pointerId in stale) {
			active.remove(pointerId);
			revision++;
		}
	}

	public function pointerMove(event:UiEvent):Void {
		var sequence = active.get(event.pointerId);
		if (sequence == null)
			return;
		revision++;
		sequence.x = event.x;
		sequence.y = event.y;
		var dx = event.x - sequence.startX;
		var dy = event.y - sequence.startY;
		var distanceSquared = dx * dx + dy * dy;
		var movementTolerance = 8.0;
		for (recognizer in sequence.recognizers)
			if ((recognizer.kind == GestureKind.Tap || recognizer.kind == GestureKind.LongPress) &&
				recognizer.threshold < movementTolerance)
				movementTolerance = recognizer.threshold;
		if (distanceSquared > movementTolerance * movementTolerance)
			sequence.moved = true;
		for (recognizer in sequence.recognizers) {
			if (recognizer.kind != GestureKind.Drag || distanceSquared <= recognizer.threshold * recognizer.threshold)
				continue;
			var drag:DragRecognizer = cast recognizer;
			if (!sequence.dragging) {
				sequence.dragging = true;
				if (drag.hasStart)
					drag.onStart(makeEvent(GestureKind.DragStart, sequence, dx, dy));
			}
			if (drag.hasMove)
				drag.onMove(makeEvent(GestureKind.Drag, sequence, dx, dy));
		}
	}

	public function pointerUp(event:UiEvent):Void {
		var sequence = active.get(event.pointerId);
		active.remove(event.pointerId);
		if (sequence == null)
			return;
		sequence.x = event.x;
		sequence.y = event.y;
		var dx = event.x - sequence.startX;
		var dy = event.y - sequence.startY;
		if (sequence.dragging) {
			for (recognizer in sequence.recognizers)
				if (recognizer.kind == GestureKind.Drag) {
					var drag:DragRecognizer = cast recognizer;
					if (drag.hasEnd)
						drag.onEnd(makeEvent(GestureKind.DragEnd, sequence, dx, dy));
				}
			return;
		}
		if (sequence.longPressed || sequence.moved)
			return;
		var last = lastTaps.get(event.pointerId);
		for (recognizer in sequence.recognizers)
			if (recognizer.kind == GestureKind.DoubleTap && last != null &&
				last.target.equals(sequence.target) &&
				elapsedSeconds - last.time <= recognizer.interval) {
				var tapDx = event.x - last.x;
				var tapDy = event.y - last.y;
				if (tapDx * tapDx + tapDy * tapDy <= recognizer.threshold * recognizer.threshold) {
					if (recognizer.hasHandler)
						recognizer.onRecognized(makeEvent(GestureKind.DoubleTap, sequence, 0.0, 0.0));
				}
			}
		for (recognizer in sequence.recognizers)
			if (recognizer.kind == GestureKind.Tap && recognizer.hasHandler)
				recognizer.onRecognized(makeEvent(GestureKind.Tap, sequence, 0.0, 0.0));
		lastTaps.set(event.pointerId, new LastTap(sequence.target, event.x, event.y, elapsedSeconds));
	}

	public function pointerCancel(pointerId:Int):Void
		if (active.remove(pointerId))
			revision++;

	/** Advances gesture timers by the current frame's non-negative delta time. */
	public function advance(deltaSeconds:Float):Void {
		if (!finite(deltaSeconds) || deltaSeconds < 0.0)
			throw "Gesture time must be finite and non-negative";
		var hadActive = false;
		for (_ in active.keys()) {
			hadActive = true;
			break;
		}
		elapsedSeconds += deltaSeconds;
		for (sequence in active) {
			sequence.elapsed += deltaSeconds;
			if (sequence.longPressed || sequence.moved)
				continue;
			for (recognizer in sequence.recognizers)
				if (recognizer.kind == GestureKind.LongPress &&
					sequence.elapsed >= recognizer.interval) {
					sequence.longPressed = true;
					if (recognizer.hasHandler)
						recognizer.onRecognized(makeEvent(GestureKind.LongPress, sequence, 0.0, 0.0));
			}
		}
		if (hadActive)
			revision++;
	}

	public function cancelAll():Void {
		var hasState = false;
		for (_ in active.keys()) {
			hasState = true;
			break;
		}
		if (!hasState)
			for (_ in lastTaps.keys()) {
				hasState = true;
				break;
			}
		if (hasState) {
			active.clear();
			lastTaps.clear();
			revision++;
		}
	}

	function makeEvent(kind:Int, sequence:GestureSequence, deltaX:Float,
			deltaY:Float):GestureEvent
		return new GestureEvent(kind, sequence.target, sequence.pointerId, sequence.startX,
			sequence.startY, sequence.x, sequence.y, deltaX, deltaY, sequence.elapsed);

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
