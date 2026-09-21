package components;

import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import Canvas;
import Color;
import ResolvedLayoutItem;
import nativekit.ui.animation.Animation;
import nativekit.ui.animation.AnimationHandle;
import nativekit.ui.animation.AnimationScheduler;
import nativekit.ui.animation.LoopAnimation;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Lightweight animated preview used by the showcase without a production GPU producer. */
class CubeView implements View {
	public final key:String;
	public final style:LayoutStyle;
	public final rotation:Float;
	public final label:String;
	public final animated:Bool;
	public final speed:Float;

	public function new(key:String, rotation:Float, label:String, ?style:LayoutStyle,
			animated:Bool = false, speed:Float = 0.12) {
		if (key == null || key.length == 0 || label == null || label.length == 0 ||
			!Math.isFinite(rotation) || !Math.isFinite(speed) || speed <= 0.0)
			throw "Cube views require a stable key, finite rotation, and accessible label";
		this.key = key;
		this.rotation = rotation;
		this.label = label;
		this.style = style == null ? defaultStyle() : style.copy();
		this.animated = animated;
		this.speed = speed;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var id = context.id("cube-view");
			var painterState:State<CubeViewPainter> = context.resourceState(id,
				function() return new CubeViewPainter(context.animations, rotation, speed),
				function(value:CubeViewPainter) { value.dispose(); });
			var painter = painterState.value;
			painter.configure(rotation, speed, animated);
			var node = new RenderNode(id, LayoutVisualKind.Custom, style);
			node.semantics = new Semantics(AccessibilityRole.Image, label);
			node.onPaint(function(canvas, geometry) painter.paint(canvas, geometry));
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(170.0);
		result.clipToParent = true;
		return result;
	}
}

private class CubeViewPainter {
	static inline var TwoPi:Float = 6.283185307179586;
	final scheduler:AnimationScheduler;
	final loop:LoopAnimation;
	final animation:Animation;
	var registration:Null<AnimationHandle>;
	var baseRotation:Float;

	public function new(scheduler:AnimationScheduler, rotation:Float, speed:Float) {
		this.scheduler = scheduler;
		baseRotation = rotation;
		loop = new LoopAnimation(speed);
		animation = loop;
		registration = null;
	}

	public function configure(rotation:Float, speed:Float, running:Bool):Void {
		baseRotation = rotation;
		loop.speed = speed;
		if (running) {
			if (registration == null || !registration.active)
				registration = scheduler.track(animation);
		} else if (registration != null) {
			registration.cancel();
			registration = null;
		}
	}

	public function paint(canvas:Canvas, geometry:ResolvedLayoutItem):Void {
		if (geometry.width <= 0.0 || geometry.height <= 0.0)
			return;
		var phase = baseRotation + loop.phase * TwoPi;
		canvas.fillRoundedRect(new Rect(0.0, 0.0, geometry.width, geometry.height), 6.0,
			Color.rgba(0.035, 0.055, 0.09, 1.0));
		var inset = 12.0 + 5.0 * Math.sin(phase);
		canvas.fillRoundedRect(new Rect(inset, inset, Math.max(0.0, geometry.width - inset * 2.0),
			Math.max(0.0, geometry.height - inset * 2.0)), 4.0,
			Color.rgba(0.20, 0.65, 0.84, 0.72));
	}

	public function dispose():Void {
		if (registration != null)
			registration.cancel();
		registration = null;
	}
}
