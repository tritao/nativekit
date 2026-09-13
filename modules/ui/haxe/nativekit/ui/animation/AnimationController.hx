package nativekit.ui.animation;

import nativekit.ui.animation.Animation;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.animation.Easing;
import nativekit.ui.animation.Tween;

/** Time-based scalar tween controller retained in Haxe widget state. */
class AnimationController implements Animation {
	public var value(default, null):Float;
	public var active(default, null):Bool;
	public var onUpdate:Float->Void;
	public var onComplete:Void->Void;
	var hasUpdate:Bool;
	var hasComplete:Bool;
	var scheduler:Null<AnimationScheduler>;
	var tween:Null<Tween>;
	var elapsed:Float;

	public function new(?scheduler:AnimationScheduler, ?onUpdate:Float->Void,
			?onComplete:Void->Void) {
		this.scheduler = scheduler;
		this.onUpdate = onUpdate == null ? function(_) {} : onUpdate;
		this.onComplete = onComplete == null ? function() {} : onComplete;
		hasUpdate = onUpdate != null;
		hasComplete = onComplete != null;
		value = 0.0;
		active = false;
		tween = null;
		elapsed = 0.0;
	}

	public function attach(scheduler:AnimationScheduler):Void {
		if (scheduler == null)
			throw "Animation controller requires a scheduler";
		this.scheduler = scheduler;
		if (active)
			scheduler.track(this);
	}

	public function play(from:Float, to:Float, duration:Float,
			easing:Int = Easing.Linear):Void {
		var next = new Tween(from, to, duration, easing);
		value = from;
		elapsed = 0.0;
		tween = next;
		active = duration > 0.0;
		if (active) {
			if (scheduler != null)
				scheduler.track(this);
		} else {
			value = to;
			if (hasUpdate)
				onUpdate(value);
			if (hasComplete)
				onComplete();
		}
	}

	public function stop():Void {
		active = false;
		if (scheduler != null)
			scheduler.remove(this);
	}

	public function advance(deltaSeconds:Float):Bool {
		if (!active || tween == null)
			return false;
		elapsed += deltaSeconds;
		var completed = elapsed >= tween.duration;
		value = completed ? tween.to : tween.valueAt(elapsed);
		if (hasUpdate)
			onUpdate(value);
		if (completed) {
			active = false;
			if (hasComplete)
				onComplete();
			return false;
		}
		return true;
	}
}
