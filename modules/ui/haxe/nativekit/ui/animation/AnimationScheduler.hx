package nativekit.ui.animation;

import nativekit.ui.animation.Animation;

/** Advances active Haxe animations whenever the host drives a UI frame. */
class AnimationScheduler {
	final active:Array<AnimationHandle>;
	public var onFrameRequested:Null<Void->Void>;
	public var revision(default, null):Int;

	public function new() {
		active = [];
		onFrameRequested = null;
		revision = 0;
	}

	/** Registers an animation and returns a stable handle for its lifetime. */
	public function track(animation:Animation):AnimationHandle {
		if (animation == null)
			throw "Cannot track a null animation";
		for (handle in active)
			if (handle.matches(animation))
				return handle;
		var handle = new AnimationHandle(this, animation);
		active.push(handle);
		revision++;
		requestFrame();
		return handle;
	}

	/** Compatibility removal for callers that still retain the animation object. */
	public function remove(animation:Animation):Void {
		if (animation == null)
			return;
		var pending:Array<AnimationHandle> = [];
		for (handle in active)
			if (handle.matches(animation))
				pending.push(handle);
		for (handle in pending)
			removeHandle(handle);
	}

	@:allow(nativekit.ui.animation.AnimationHandle)
	function removeHandle(handle:AnimationHandle):Void {
		if (handle == null)
			return;
		if (active.remove(handle)) {
			handle.deactivate();
			revision++;
		}
	}

	public function advance(deltaSeconds:Float):Void {
		if (!finite(deltaSeconds) || deltaSeconds < 0.0)
			throw "Animation time must be finite and non-negative";
		var hadActive = active.length > 0;
		var current = active.copy();
		for (handle in current)
			if (active.indexOf(handle) >= 0 && !handle.advance(deltaSeconds))
				removeHandle(handle);
		if (active.length > 0)
			requestFrame();
		if (hadActive)
			revision++;
	}

	public function cancelAll():Void {
		if (active.length == 0)
			return;
		for (handle in active)
			handle.deactivate();
		active.resize(0);
		revision++;
	}

	public var activeCount(get, never):Int;
	inline function get_activeCount():Int
		return active.length;

	function requestFrame():Void {
		if (onFrameRequested != null)
			onFrameRequested();
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
