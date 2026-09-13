package nativekit.ui.animation;

import nativekit.ui.animation.Animation;
import nativekit.ui.animation.AnimationScheduler;

/** Damped scalar spring simulated in Haxe with bounded semi-implicit steps. */
class SpringController implements Animation {
	public var value(default, null):Float;
	public var velocity(default, null):Float;
	public var target(default, null):Float;
	public var active(default, null):Bool;
	public final stiffness:Float;
	public final damping:Float;
	public final mass:Float;
	public final tolerance:Float;
	public var onUpdate:Float->Void;
	var hasUpdate:Bool;
	var scheduler:Null<AnimationScheduler>;

	public function new(initial:Float, stiffness:Float = 180.0, damping:Float = 24.0,
			mass:Float = 1.0, tolerance:Float = 0.001,
			?scheduler:AnimationScheduler, ?onUpdate:Float->Void) {
		if (!finite(initial) || !finite(stiffness) || !finite(damping) || !finite(mass) ||
			!finite(tolerance) || stiffness <= 0.0 || damping < 0.0 || mass <= 0.0 || tolerance <= 0.0)
			throw "Spring parameters must be finite and physically valid";
		value = initial;
		velocity = 0.0;
		target = initial;
		active = false;
		this.stiffness = stiffness;
		this.damping = damping;
		this.mass = mass;
		this.tolerance = tolerance;
		this.scheduler = scheduler;
		this.onUpdate = onUpdate == null ? function(_) {} : onUpdate;
		hasUpdate = onUpdate != null;
	}

	public function setTarget(value:Float, initialVelocity:Float = 0.0):Void {
		if (!finite(value) || !finite(initialVelocity))
			throw "Spring target and velocity must be finite";
		target = value;
		velocity = initialVelocity;
		active = true;
		if (scheduler != null)
			scheduler.track(this);
	}

	public function attach(scheduler:AnimationScheduler):Void {
		if (scheduler == null)
			throw "Spring controller requires a scheduler";
		this.scheduler = scheduler;
		if (active)
			scheduler.track(this);
	}

	public function stop():Void {
		active = false;
		velocity = 0.0;
		if (scheduler != null)
			scheduler.remove(this);
	}

	public function advance(deltaSeconds:Float):Bool {
		if (!active)
			return false;
		if (!finite(deltaSeconds) || deltaSeconds < 0.0)
			throw "Spring time must be finite and non-negative";
		var remaining = deltaSeconds;
		while (remaining > 0.0) {
			var step = remaining > 1.0 / 120.0 ? 1.0 / 120.0 : remaining;
			var acceleration = (stiffness * (target - value) - damping * velocity) / mass;
			velocity += acceleration * step;
			value += velocity * step;
			remaining -= step;
		}
		if (lessThanAbsolute(value - target, tolerance) &&
			lessThanAbsolute(velocity, tolerance)) {
			value = target;
			velocity = 0.0;
			active = false;
		}
		if (hasUpdate)
			onUpdate(value);
		return active;
	}

	static inline function lessThanAbsolute(value:Float, threshold:Float):Bool
		return (value < 0.0 ? -value : value) < threshold;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
