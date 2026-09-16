package nativekit.ui.widgets;

import Canvas;
import Color;
import LayoutAxis;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import ResolvedLayoutItem;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityActionData;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Horizontal value slider with Haxe-owned pointer, keyboard, and semantic behavior. */
class Slider implements View {
	public final key:String;
	public final label:String;
	public var value:Float;
	public final minimum:Float;
	public final maximum:Float;
	public final step:Float;
	public var enabled:Bool;
	public var onChange:Null<Float->Void>;
	public final style:LayoutStyle;

	public function new(key:String, label:String, value:Float, minimum:Float = 0.0,
			maximum:Float = 1.0, step:Float = 0.01, ?onChange:Float->Void,
			?style:LayoutStyle) {
		if (key == null || key.length == 0 || !finite(minimum) || !finite(maximum) ||
			!finite(value) || !finite(step) || maximum <= minimum || step <= 0.0)
			throw "Slider values and range are invalid";
		this.key = key;
		this.label = label == null ? "" : label;
		this.minimum = minimum;
		this.maximum = maximum;
		this.step = step;
		this.value = clamp(value, minimum, maximum);
		this.onChange = onChange;
		this.style = style == null ? defaultStyle() : style.copy();
		enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id("slider"), LayoutVisualKind.Custom, style);
			node.focusable = enabled;
			node.enabled = enabled;
			var semantics = new Semantics(AccessibilityRole.Slider, label, Std.string(value));
			semantics.actions = AccessibilityAction.SetValue | AccessibilityAction.Increment |
				AccessibilityAction.Decrement;
			semantics.numericValue = value;
			semantics.numericMinimum = minimum;
			semantics.numericMaximum = maximum;
			node.semantics = semantics;

			var invalidation:State<Float> = context.state(node.id, value);
			var dragging:State<Bool> = context.state(context.id("dragging"), false);
			var setValue = function(next:Float) {
				if (!enabled || !finite(next))
					return false;
				var bounded = clamp(next, minimum, maximum);
				var snapped = minimum + Std.int((bounded - minimum) / step + 0.5) * step;
				bounded = clamp(snapped, minimum, maximum);
				if (bounded == value)
					return false;
				value = bounded;
				semantics.value = Std.string(value);
				semantics.numericValue = value;
				invalidation.update(value);
				if (onChange != null)
					onChange(value);
				return true;
			};
			var valueAtPointer = function(event:UiEvent) {
				if (node.resolved == null)
					return false;
				var geometry:ResolvedLayoutItem = cast node.resolved;
				var point = geometry.viewportToLayout(event.x, event.y);
				var usableWidth = Math.max(1.0, geometry.width - 20.0);
				var fraction = clamp((point.x - geometry.x - 10.0) / usableWidth, 0.0, 1.0);
				return setValue(minimum + fraction * (maximum - minimum));
			};
			node.onPaint(function(canvas, geometry) {
				var accent = enabled ? context.theme.accent : context.theme.controlDisabled;
				var muted = context.theme.controlColor(false, enabled);
				var foreground = context.theme.textColor(enabled);
				var y = geometry.height * 0.5;
				var start = 10.0;
				var end = Math.max(start + 1.0, geometry.width - 10.0);
				var knob = start + (end - start) * ((value - minimum) / (maximum - minimum));
				canvas.fillRect(new Rect(start, y - 2.0, end - start, 4.0),
					muted);
				canvas.fillRect(new Rect(start, y - 2.0, Math.max(1.0, knob - start), 4.0),
					accent);
				canvas.fillRect(new Rect(knob - 6.0, y - 8.0, 12.0, 16.0),
					foreground);
			});
			node.on(UiEventKind.PointerDown, function(event) {
				if (enabled && event.button == 0) {
					event.capturePointer();
					dragging.update(true);
					valueAtPointer(event);
					event.preventDefault();
				}
			});
			node.on(UiEventKind.PointerMove, function(event) {
				var isDragging:Bool = dragging.value;
				if (isDragging)
					valueAtPointer(event);
			});
			node.on(UiEventKind.PointerUp, function(_) { dragging.update(false); });
			node.on(UiEventKind.PointerCancel, function(_) { dragging.update(false); });

			var handleKey = function(event:UiEvent) {
				if (!enabled)
					return;
				var handled = true;
				if (event.key == UiKey.Left || event.key == UiKey.Down)
					setValue(value - step);
				else if (event.key == UiKey.Right || event.key == UiKey.Up)
					setValue(value + step);
				else if (event.key == UiKey.Home)
					setValue(minimum);
				else if (event.key == UiKey.End)
					setValue(maximum);
				else
					handled = false;
				if (handled)
					event.preventDefault();
			};
			node.on(UiEventKind.KeyDown, handleKey);
			node.on(UiEventKind.KeyRepeat, handleKey);
			node.on(UiEventKind.AccessibilitySetValue, function(event) {
				if (event.text != null) {
					var parsed = parseNumericValue(event.text);
					if (parsed != null)
						setValue(parsed);
				}
			});
			node.on(UiEventKind.AccessibilityIncrement, function(event) {
				var action:AccessibilityActionData = cast event.data;
				setValue(value + step * (action != null && action.granularity > 1 ? action.granularity : 1));
			});
			node.on(UiEventKind.AccessibilityDecrement, function(event) {
				var action:AccessibilityActionData = cast event.data;
				setValue(value - step * (action != null && action.granularity > 1 ? action.granularity : 1));
			});
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.fixed(220.0);
		result.height = LayoutAxis.fixed(32.0);
		return result;
	}

	static inline function clamp(value:Float, minimum:Float, maximum:Float):Float
		return value < minimum ? minimum : value > maximum ? maximum : value;

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;

	static function parseNumericValue(value:String):Null<Float> {
		var text = value;
		var start = 0;
		var end = text.length;
		while (start < end && whitespace(text.charCodeAt(start)))
			start++;
		while (end > start && whitespace(text.charCodeAt(end - 1)))
			end--;
		if (start == end)
			return null;
		var index = start;
		var sign = 1.0;
		var first = text.charCodeAt(index);
		if (first == 45 || first == 43) {
			if (first == 45)
				sign = -1.0;
			index++;
		}
		var whole = 0.0;
		var digits = 0;
		while (index < end) {
			var code = text.charCodeAt(index);
			if (code < 48 || code > 57)
				break;
			whole = whole * 10.0 + code - 48;
			digits++;
			index++;
		}
		var fraction = 0.0;
		var fractionScale = 0.1;
		if (index < end && text.charCodeAt(index) == 46) {
			index++;
			while (index < end) {
				var code = text.charCodeAt(index);
				if (code < 48 || code > 57)
					break;
				fraction += (code - 48) * fractionScale;
				fractionScale *= 0.1;
				digits++;
				index++;
			}
		}
		if (digits == 0)
			return null;
		var exponentSign = 1;
		var exponent = 0;
		if (index < end && (text.charCodeAt(index) == 101 || text.charCodeAt(index) == 69)) {
			index++;
			if (index < end && (text.charCodeAt(index) == 45 || text.charCodeAt(index) == 43)) {
				if (text.charCodeAt(index) == 45)
					exponentSign = -1;
				index++;
			}
			var exponentDigits = 0;
			while (index < end) {
				var code = text.charCodeAt(index);
				if (code < 48 || code > 57)
					break;
				exponent = exponent * 10 + code - 48;
				if (exponent > 308)
					return null;
				exponentDigits++;
				index++;
			}
			if (exponentDigits == 0)
				return null;
		}
		if (index != end)
			return null;
		var result = sign * (whole + fraction);
		var scale = 1.0;
		for (_ in 0...exponent)
			scale = exponentSign > 0 ? scale * 10.0 : scale / 10.0;
		result *= scale;
		return finite(result) ? result : null;
	}

	static inline function whitespace(code:Int):Bool
		return code == 9 || code == 10 || code == 13 || code == 32;
}
