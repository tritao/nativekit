package nativekit.ui.widgets;

import Canvas;
import Color;
import LineCap;
import LineJoin;
import Path;
import PathBuilder;
import ResolvedLayoutItem;
import SolidPaint;
import nativekit.ui.animation.Animation;
import nativekit.ui.animation.AnimationHandle;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.animation.LoopAnimation;

/** Owns the reusable resources and command encoding for a procedural spinner. */
class SpinnerPainter {
	static inline var TwoPi:Float = 6.283185307179586;

	public var kind(default, null):SpinnerKind;
	final animation:LoopAnimation;
	final circle:Path;
	final arc:Path;
	final bar:Path;
	var paintResource:SolidPaint;

	final animationToken:Animation;
	final scheduler:AnimationScheduler;
	var registration:Null<AnimationHandle>;
	var color:Color;

	public function new(scheduler:AnimationScheduler, color:Color, speed:Float,
			kind:SpinnerKind = SpinnerKind.Ring) {
		if (scheduler == null)
			throw "Spinner painter requires an animation scheduler";
		if (color == null)
			throw "Spinner painter requires a color";
		if (!isValidKind(kind))
			throw "Spinner kind is invalid";
		if (!finite(speed) || speed <= 0.0)
			throw "Spinner speed must be finite and positive";
		this.scheduler = scheduler;
		this.color = color;
		this.kind = kind;
		animation = new LoopAnimation(speed);
		animationToken = animation;
		registration = null;

		var madeCircle:Path = null;
		var madeArc:Path = null;
		var madeBar:Path = null;
		var madePaint:SolidPaint = null;
		try {
			madeCircle = createCircle();
			madeArc = createArc();
			madeBar = createBar();
			madePaint = SolidPaint.create(color);
		} catch (error:Dynamic) {
			if (madePaint != null)
				madePaint.dispose();
			if (madeBar != null)
				madeBar.dispose();
			if (madeArc != null)
				madeArc.dispose();
			if (madeCircle != null)
				madeCircle.dispose();
			throw error;
		}
		circle = cast madeCircle;
		arc = cast madeArc;
		bar = cast madeBar;
		paintResource = cast madePaint;
	}

	/** Updates paint, speed, and kind while retaining the resource identity. */
	public function configure(nextKind:SpinnerKind, nextColor:Color, speed:Float):Void {
		if (!isValidKind(nextKind))
			throw "Spinner kind is invalid";
		if (nextColor == null)
			throw "Spinner painter requires a color";
		if (!finite(speed) || speed <= 0.0)
			throw "Spinner speed must be finite and positive";
		kind = nextKind;
		animation.speed = speed;
		if (!sameColor(color, nextColor)) {
			var nextPaint = SolidPaint.create(nextColor);
			var previous = paintResource;
			paintResource = nextPaint;
			color = nextColor;
			previous.dispose();
		}
	}

	/** Starts or pauses scheduling without changing the current animation phase. */
	public function setRunning(running:Bool):Void {
		if (running) {
			if (registration == null || !registration.active)
				registration = scheduler.track(animationToken);
		} else if (registration != null) {
			registration.cancel();
			registration = null;
		}
	}

	/** Encodes the current spinner frame into a canvas command stream. */
	public function paintFrame(canvas:Canvas, geometry:ResolvedLayoutItem):Void {
		if (canvas == null || geometry == null)
			throw "Spinner painting requires a canvas and geometry";
		var size = Math.min(geometry.width, geometry.height);
		if (size <= 0.0)
			return;

		canvas.withState(function(target) {
			target.translate(geometry.width * 0.5, geometry.height * 0.5);
			target.scale(size * 0.42, size * 0.42);
			switch (kind) {
				case SpinnerKind.Ring:
					target.rotate(animation.phase * TwoPi);
					target.stroke(arc, paintResource, 0.16, LineCap.Round, LineJoin.Round);
				case SpinnerKind.Dots:
					paintDots(target);
				case SpinnerKind.Bars:
					paintBars(target);
				case SpinnerKind.Pulse:
					paintPulse(target);
			}
		});
	}

	/** Releases the scheduler registration and all native drawing resources. */
	public function dispose():Void {
		if (registration != null)
			registration.cancel();
		registration = null;
		paintResource.dispose();
		bar.dispose();
		arc.dispose();
		circle.dispose();
	}

	function paintDots(canvas:Canvas):Void {
		for (index in 0...12) {
			var normalized = index / 12.0;
			var distance = normalized - animation.phase;
			if (distance < 0.0)
				distance += 1.0;
			var alpha = 0.18 + 0.82 * (1.0 - distance);
			var angle = normalized * TwoPi - Math.PI * 0.5;
			canvas.withState(function(dot) {
				dot.translate(Math.cos(angle) * 0.74, Math.sin(angle) * 0.74);
				dot.scale(0.11, 0.11);
				dot.setAlpha(alpha);
				dot.fill(circle, paintResource);
			});
		}
	}

	function paintBars(canvas:Canvas):Void {
		for (index in 0...12) {
			var normalized = index / 12.0;
			var distance = normalized - animation.phase;
			if (distance < 0.0)
				distance += 1.0;
			var alpha = 0.18 + 0.82 * (1.0 - distance);
			var angle = normalized * TwoPi;
			canvas.withState(function(barCanvas) {
				barCanvas.rotate(angle);
				barCanvas.setAlpha(alpha);
				barCanvas.fill(bar, paintResource);
			});
		}
	}

	function paintPulse(canvas:Canvas):Void {
		paintPulseCircle(canvas, animation.phase);
		var delayed = animation.phase + 0.5;
		if (delayed >= 1.0)
			delayed -= 1.0;
		paintPulseCircle(canvas, delayed);
	}

	function paintPulseCircle(canvas:Canvas, progress:Float):Void {
		canvas.withState(function(circleCanvas) {
			circleCanvas.scale(0.16 + progress * 0.60, 0.16 + progress * 0.60);
			circleCanvas.setAlpha(0.68 * (1.0 - progress));
			circleCanvas.fill(circle, paintResource);
		});
	}

	static function createCircle():Path {
		var k = 0.5522847498;
		return new PathBuilder().moveTo(1.0, 0.0)
			.cubicTo(1.0, k, k, 1.0, 0.0, 1.0)
			.cubicTo(-k, 1.0, -1.0, k, -1.0, 0.0)
			.cubicTo(-1.0, -k, -k, -1.0, 0.0, -1.0)
			.cubicTo(k, -1.0, 1.0, -k, 1.0, 0.0)
			.close().build();
	}

	static function createArc():Path {
		var k = 0.5522847498;
		return new PathBuilder().moveTo(0.0, -1.0)
			.cubicTo(k, -1.0, 1.0, -k, 1.0, 0.0)
			.cubicTo(1.0, k, k, 1.0, 0.0, 1.0)
			.cubicTo(-k, 1.0, -1.0, k, -1.0, 0.0).build();
	}

	static function createBar():Path {
		return new PathBuilder().moveTo(-0.11, -0.94).lineTo(0.11, -0.94)
			.lineTo(0.11, -0.58).lineTo(-0.11, -0.58).close().build();
	}

	public static function isValidKind(value:SpinnerKind):Bool
		return value == SpinnerKind.Ring || value == SpinnerKind.Dots ||
			value == SpinnerKind.Bars || value == SpinnerKind.Pulse;

	static function sameColor(left:Color, right:Color):Bool
		return left.red == right.red && left.green == right.green &&
			left.blue == right.blue && left.alpha == right.alpha;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
