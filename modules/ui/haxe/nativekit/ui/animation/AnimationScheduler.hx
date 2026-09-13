package nativekit.ui.animation;

import nativekit.ui.animation.Animation;

/** Advances active Haxe animations once for each submitted UI frame. */
class AnimationScheduler {
	final active:Array<Animation>;

	public function new() {
		active = [];
	}

	public function track(animation:Animation):Void {
		if (animation == null)
			throw "Cannot track a null animation";
		if (active.indexOf(animation) < 0)
			active.push(animation);
	}

	public function remove(animation:Animation):Void
		active.remove(animation);

	public function advance(deltaSeconds:Float):Void {
		if (!finite(deltaSeconds) || deltaSeconds < 0.0)
			throw "Animation time must be finite and non-negative";
		var current = active.copy();
		for (animation in current)
			if (active.indexOf(animation) >= 0 && !animation.advance(deltaSeconds))
				active.remove(animation);
	}

	public function cancelAll():Void
		active.resize(0);

	public var activeCount(get, never):Int;
	inline function get_activeCount():Int
		return active.length;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
