package nativekit.ui.core;

/** Routed event shared by capture, target, and bubble handlers. */
class UiEvent {
	public final kind:String;
	public final target:WidgetId;
	public final x:Float;
	public final y:Float;
	public final deltaX:Float;
	public final deltaY:Float;
	public final button:Int;
	public final key:Int;
	public final scancode:Int;
	public final modifiers:Int;
	public final pointerId:Int;
	public final timestamp:Float;
	public final text:Null<String>;
	public final data:Dynamic;
	public var currentTarget:Null<WidgetId>;
	public var phase:String;
	public var defaultPrevented(default, null):Bool;
	public var propagationStopped(default, null):Bool;
	public var pointerCaptureTarget(default, null):Null<WidgetId>;
	public var pointerReleaseRequested(default, null):Bool;

	public function new(kind:String, target:WidgetId, x:Float = 0.0, y:Float = 0.0,
			deltaX:Float = 0.0, deltaY:Float = 0.0, button:Int = 0, key:Int = 0,
			modifiers:Int = 0, text:Null<String> = null, data:Dynamic = null,
			scancode:Int = 0, pointerId:Int = 0, timestamp:Float = -1.0) {
		if (kind == null || kind.length == 0 || target == null)
			throw "Routed events require a kind and target";
		this.kind = kind;
		this.target = target;
		this.x = x;
		this.y = y;
		this.deltaX = deltaX;
		this.deltaY = deltaY;
		this.button = button;
		this.key = key;
		this.scancode = scancode;
		this.modifiers = modifiers;
		this.pointerId = pointerId;
		this.timestamp = timestamp;
		this.text = text;
		this.data = data;
		currentTarget = null;
		phase = "target";
		defaultPrevented = false;
		propagationStopped = false;
		pointerCaptureTarget = null;
		pointerReleaseRequested = false;
	}

	public function stopPropagation():Void
		propagationStopped = true;

	public function preventDefault():Void
		defaultPrevented = true;

	/** Routes this pointer sequence to the current handler until release or cancellation. */
	public function capturePointer():Void {
		pointerCaptureTarget = currentTarget == null ? target : currentTarget;
		pointerReleaseRequested = false;
	}

	/** Releases explicit pointer capture after the current event finishes dispatching. */
	public function releasePointer():Void {
		pointerCaptureTarget = null;
		pointerReleaseRequested = true;
	}
}
