package nativekit.ui.widgets;

import Canvas;
import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Read-only progress indicator painted from a normalized numeric range. */
class ProgressBar implements View {
	public final key:String;
	public final label:Null<String>;
	public var value:Float;
	public final minimum:Float;
	public final maximum:Float;
	public final style:LayoutStyle;

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
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("progress"), LayoutVisualKind.Custom, style);
			if (label != null) {
				var semantics = new Semantics(AccessibilityRole.Group, label, Std.string(value));
				semantics.states = AccessibilityState.ReadOnly;
				semantics.numericValue = value;
				semantics.numericMinimum = minimum;
				semantics.numericMaximum = maximum;
				node.semantics = semantics;
			}
			node.onPaint(function(canvas, geometry) {
				var fraction = (value - minimum) / (maximum - minimum);
				canvas.fillRect(new Rect(0.0, 0.0, geometry.width, geometry.height),
					Color.rgba(0.19, 0.21, 0.25, 1.0));
				var fillWidth = geometry.width * fraction;
				if (fillWidth > 0.0)
					canvas.fillRect(new Rect(0.0, 0.0, fillWidth, geometry.height),
						Color.rgba(0.22, 0.52, 0.84, 1.0));
			});
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(200.0);
		result.height = LayoutAxis.fixed(14.0);
		result.radiusTopLeft = result.radiusTopRight = 7.0;
		result.radiusBottomLeft = result.radiusBottomRight = 7.0;
		return result;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
