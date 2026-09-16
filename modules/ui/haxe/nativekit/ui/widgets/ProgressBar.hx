package nativekit.ui.widgets;

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
import nativekit.ui.style.StyleProperty;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.widgets.ProgressPainter;

/** Read-only progress indicator painted from a normalized numeric range. */
class ProgressBar implements View {
	public final key:String;
	public final label:Null<String>;
	public var value:Float;
	public final minimum:Float;
	public final maximum:Float;
	public final style:LayoutStyle;
	public var mode:ProgressMode;
	/** Seconds used when a determinate value changes; zero disables interpolation. */
	public var animationDuration:Float;

	public function new(key:String, value:Float, minimum:Float = 0.0,
			maximum:Float = 1.0, ?label:String, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || !finite(value) || !finite(minimum) ||
			!finite(maximum) || maximum <= minimum)
			throw "Progress values and range are invalid";
		this.key = key;
		this.label = label;
		this.value = clamp(value, minimum, maximum);
		this.minimum = minimum;
		this.maximum = maximum;
		this.style = style == null ? defaultStyle() : style.copy();
		mode = ProgressMode.Determinate;
		animationDuration = 0.22;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var nodeId = context.id("progress");
			var computed = context.styleResolver.resolve(
				new StyleTarget("progress-bar", key, key, null, ["progress-bar"],
					context.interactionStates.get(nodeId)),
				context.inheritedStyle, context.theme.styles, context.styleSheet, style, context.environment);
			var node = new RenderNode(nodeId, LayoutVisualKind.Custom, computed.toLayoutStyle());
			node.setStyleIdentity("progress-bar", key, key, null, ["progress-bar"]);
			node.states = context.interactionStates.get(nodeId);
			node.computedStyle = computed;
			var indeterminate = mode == ProgressMode.Indeterminate;
			var semantics = new Semantics(AccessibilityRole.ProgressBar, label,
				indeterminate ? "Indeterminate" : Std.string(value));
			semantics.states = indeterminate ? AccessibilityState.Busy : AccessibilityState.ReadOnly;
			if (!indeterminate) {
				semantics.numericValue = value;
				semantics.numericMinimum = minimum;
				semantics.numericMaximum = maximum;
			}
			node.semantics = semantics;
			var fraction = (value - minimum) / (maximum - minimum);
			var stored:State<ProgressPainter> = context.resourceState(node.id,
				function() return new ProgressPainter(context.animations, fraction),
				function(painter:ProgressPainter) painter.dispose());
			var painter:ProgressPainter = cast stored.value;
			painter.configure(mode, fraction, animationDuration, context.environment.reducedMotion);
			node.onPaint(function(canvas, geometry) painter.paint(canvas, geometry,
				computed.get(StyleProperty.ProgressTrackColor),
				computed.get(StyleProperty.ProgressFillColor)));
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(200.0);
		result.height = LayoutAxis.fixed(8.0);
		result.radiusTopLeft = result.radiusTopRight = 4.0;
		result.radiusBottomLeft = result.radiusBottomRight = 4.0;
		return result;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
