package nativekit.ui.animation;

import nativekit.ui.animation.Easing;

/** Immutable scalar tween sampled by an AnimationController. */
class Tween {
	public final from:Float;
	public final to:Float;
	public final duration:Float;
	public final easing:Int;

	public function new(from:Float, to:Float, duration:Float, easing:Int = Easing.Linear) {
		if (!finite(from) || !finite(to) || !finite(duration) || duration < 0.0 ||
			easing < Easing.Linear || easing > Easing.EaseInOut)
			throw "Tween endpoints, duration, and easing must be valid";
		this.from = from;
		this.to = to;
		this.duration = duration;
		this.easing = easing;
	}

	public function valueAt(seconds:Float):Float {
		if (!finite(seconds))
			throw "Tween sample time must be finite";
		var progress = duration == 0.0 ? 1.0 : seconds / duration;
		var eased = Easing.apply(progress, easing);
		return from + (to - from) * eased;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
