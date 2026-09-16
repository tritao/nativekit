package nativekit.ui.widgets;

import Canvas;
import Color;
import Rect;
import ResolvedLayoutItem;
import nativekit.ui.animation.Animation;
import nativekit.ui.animation.AnimationController;
import nativekit.ui.animation.AnimationHandle;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.animation.Easing;
import nativekit.ui.animation.LoopAnimation;

/** Retained animation state and drawing for determinate and indeterminate progress. */
class ProgressPainter {
	final scheduler:AnimationScheduler;
	final tween:AnimationController;
	final loop:LoopAnimation;
	final loopToken:Animation;
	var loopRegistration:Null<AnimationHandle>;
	var target:Float;
	var mode:ProgressMode;
	var reducedMotion:Bool;

	public function new(scheduler:AnimationScheduler, fraction:Float) {
		this.scheduler = scheduler;
		target = clamp(fraction);
		tween = new AnimationController(scheduler);
		tween.play(target, target, 0.0);
		loop = new LoopAnimation(0.85);
		loopToken = loop;
		loopRegistration = null;
		mode = ProgressMode.Determinate;
		reducedMotion = false;
	}

	public function configure(nextMode:ProgressMode, fraction:Float, duration:Float,
			reduceMotion:Bool):Void {
		var previousMode = mode;
		mode = nextMode;
		reducedMotion = reduceMotion;
		if (mode == ProgressMode.Indeterminate) {
			tween.stop();
			if (reducedMotion)
				stopLoop();
			else if (loopRegistration == null || !loopRegistration.active)
				loopRegistration = scheduler.track(loopToken);
			return;
		}
		stopLoop();
		var next = clamp(fraction);
		if (reduceMotion) {
			target = next;
			if (tween.value != next)
				tween.play(next, next, 0.0);
			return;
		}
		if (next == target && previousMode == ProgressMode.Determinate)
			return;
		target = next;
		if (duration <= 0.0)
			tween.play(next, next, 0.0);
		else
			tween.play(tween.value, next, duration, Easing.EaseOut);
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem,
			track:Color, fill:Color):Void {
		canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width, geometry.height), track);
		if (mode == ProgressMode.Determinate) {
			canvas.fillRectIfPositive(new Rect(0.0, 0.0, geometry.width * tween.value,
				geometry.height), fill);
			return;
		}
		var segment = geometry.width * 0.32;
		var start = reducedMotion ? (geometry.width - segment) * 0.5 :
			loop.phase * (geometry.width + segment) - segment;
		var left = Math.max(0.0, start);
		var right = Math.min(geometry.width, start + segment);
		canvas.fillRectIfPositive(new Rect(left, 0.0, right - left, geometry.height), fill);
	}

	public function dispose():Void {
		tween.stop();
		stopLoop();
	}

	function stopLoop():Void {
		if (loopRegistration != null)
			loopRegistration.cancel();
		loopRegistration = null;
	}

	static inline function clamp(value:Float):Float
		return value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
}
