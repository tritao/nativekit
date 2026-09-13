package nativekit.ui.widgets;

import Canvas;
import Color;
import Insets;
import LayoutAlignment;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import Rect;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Shared Haxe implementation for checkbox and switch-style boolean controls. */
class BinaryControl implements View {
	public final key:String;
	public final label:String;
	public var checked:Bool;
	public var enabled:Bool;
	public var onChange:Null<Bool->Void>;
	final toggle:Bool;
	final style:LayoutStyle;

	public function new(key:String, label:String, checked:Bool, toggle:Bool,
			?onChange:Bool->Void, ?style:LayoutStyle) {
		if (key == null || key.length == 0)
			throw "Boolean controls require a stable key";
		this.key = key;
		this.label = label == null ? "" : label;
		this.checked = checked;
		this.toggle = toggle;
		this.onChange = onChange;
		this.style = style == null ? defaultStyle() : style.copy();
		this.style.direction = LayoutDirection.LeftToRight;
		this.style.childAlignY = LayoutAlignment.Center;
		this.style.childGap = 9.0;
		if (this.style.height.sizing == LayoutSizing.Fit)
			this.style.height = LayoutAxis.fixed(32.0);
		this.enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var node = new RenderNode(context.id(toggle ? "toggle" : "checkbox"),
				LayoutVisualKind.Box, style);
			node.focusable = true;
			node.enabled = enabled;
			var semantics = new Semantics(AccessibilityRole.Checkbox, label,
				checked ? "true" : "false");
			semantics.actions = AccessibilityAction.Activate;
			if (checked)
				semantics.states |= AccessibilityState.Checked;
			if (!enabled)
				semantics.states |= AccessibilityState.Disabled;
			node.semantics = semantics;

			var indicatorStyle = new LayoutStyle();
			indicatorStyle.width = LayoutAxis.fixed(toggle ? 34.0 : 19.0);
			indicatorStyle.height = LayoutAxis.fixed(toggle ? 19.0 : 19.0);
			if (!toggle) {
				indicatorStyle.background = context.theme.controlColor(checked, enabled);
				indicatorStyle.radiusTopLeft = indicatorStyle.radiusTopRight = 4.0;
				indicatorStyle.radiusBottomLeft = indicatorStyle.radiusBottomRight = 4.0;
			} else {
				indicatorStyle.background = context.theme.controlColor(checked, enabled);
				indicatorStyle.radiusTopLeft = indicatorStyle.radiusTopRight = 10.0;
				indicatorStyle.radiusBottomLeft = indicatorStyle.radiusBottomRight = 10.0;
			}
			var indicator = new RenderNode(context.id("indicator"), LayoutVisualKind.Box,
				indicatorStyle);
			if (toggle) {
				indicator.onPaint(function(canvas, geometry) {
					var thumbX = checked ? geometry.width - 16.0 : 2.0;
					canvas.fillRect(new Rect(thumbX, 2.0, 15.0, 15.0),
						Color.rgba(0.98, 0.98, 0.99, 1.0));
				});
			} else {
				indicator.onPaint(function(canvas, _) {
					if (!checked)
						return;
					canvas.fillRect(new Rect(4.0, 9.0, 5.0, 2.0),
						Color.rgba(1.0, 1.0, 1.0, 1.0));
					canvas.fillRect(new Rect(8.0, 5.0, 7.0, 2.0),
						Color.rgba(1.0, 1.0, 1.0, 1.0));
				});
			}
			node.add(indicator);
			var labelNode = new RenderNode(context.id("label"), LayoutVisualKind.Text);
			labelNode.layout.text = label;
			labelNode.layout.textColor = context.theme.textColor(enabled);
			node.add(labelNode);

			var invalidation:State<Bool> = context.state(node.id, checked);
			var activate = function(event:UiEvent) {
				if (!enabled)
					return;
				checked = !checked;
				semantics.value = checked ? "true" : "false";
				if (checked)
					semantics.states |= AccessibilityState.Checked;
				else
					semantics.states &= ~AccessibilityState.Checked;
				var previous:Bool = cast invalidation.value;
				invalidation.update(!previous);
				if (onChange != null)
					onChange(checked);
			};
			node.on(UiEventKind.Click, activate);
			node.on(UiEventKind.Activate, activate);
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.padding = new Insets(4.0, 5.0, 4.0, 5.0);
		return result;
	}
}
