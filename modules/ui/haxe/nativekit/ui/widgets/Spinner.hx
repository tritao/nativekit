package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Indeterminate progress indicator rendered from reusable procedural paths. */
class Spinner implements View {
	public final key:String;
	public final label:String;
	public final kind:SpinnerKind;
	public final color:Null<Color>;
	public final speed:Float;
	public final style:LayoutStyle;
	/** When false, the spinner holds its current frame and is not scheduled. */
	public var running:Bool;

	public function new(key:String, ?label:String, ?style:LayoutStyle,
			kind:SpinnerKind = SpinnerKind.Ring, ?color:Color, speed:Float = 1.0) {
		if (key == null || key.length == 0)
			throw "Spinners require a stable key";
		if (!SpinnerPainter.isValidKind(kind))
			throw "Spinner kind is invalid";
		if (!finite(speed) || speed <= 0.0)
			throw "Spinner speed must be finite and positive";
		this.key = key;
		this.label = label == null || label.length == 0 ? "Loading" : label;
		this.kind = kind;
		this.color = color;
		this.speed = speed;
		this.style = style == null ? defaultStyle() : style.copy();
		running = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("spinner"), LayoutVisualKind.Custom, style);
			var semantics = new Semantics(AccessibilityRole.ProgressBar, label,
				running ? "Indeterminate" : "Paused");
			if (running)
				semantics.states = AccessibilityState.Busy;
			node.semantics = semantics;

			var resolvedColor = color == null ? context.theme.accent : color;
			var stored:State<SpinnerPainter> = context.resourceState(node.id,
				function() return new SpinnerPainter(context.animations, resolvedColor, speed, kind),
				function(painter:SpinnerPainter) { painter.dispose(); });
			var painter:SpinnerPainter = cast stored.value;
			painter.configure(kind, resolvedColor, speed);
			painter.setRunning(running);
			node.onPaint(function(canvas, geometry) painter.paintFrame(canvas, geometry));
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(24.0);
		result.height = LayoutAxis.fixed(24.0);
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
