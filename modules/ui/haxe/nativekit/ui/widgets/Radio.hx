package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAlignment;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Selectable radio option; use RadioGroup for exclusive selection and arrow keys. */
class Radio implements View {
	final key:Key;
	public final label:String;
	public final value:String;
	public var selected:Bool;
	public var enabled:Bool;
	public final style:LayoutStyle;
	public var onSelect:String->Void;
	public var hasSelectHandler(default, null):Bool;

	public function new(key:String, label:String, value:String, selected:Bool = false,
			?onSelect:String->Void, ?style:LayoutStyle, enabled:Bool = true) {
		if (value == null || value.length == 0)
			throw "Radio value must be non-empty";
		this.key = new Key(key);
		this.label = label == null ? "" : label;
		this.value = value;
		this.selected = selected;
		this.enabled = enabled;
		this.style = style == null ? defaultStyle() : style.copy();
		hasSelectHandler = onSelect != null;
		this.onSelect = onSelect == null ? function(_) {} : onSelect;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var node = new RenderNode(context.id("radio"), LayoutVisualKind.Box, style.copy());
			node.focusable = enabled;
			node.enabled = enabled;
			var semantics = new Semantics(AccessibilityRole.Radio, label, value);
			semantics.actions = AccessibilityAction.Activate;
			if (selected)
				semantics.states |= AccessibilityState.Selected;
			node.semantics = semantics;

			var indicatorStyle = new LayoutStyle();
			indicatorStyle.width = LayoutAxis.fixed(18.0);
			indicatorStyle.height = LayoutAxis.fixed(18.0);
			indicatorStyle.background = context.theme.controlColor(false, enabled);
			indicatorStyle.radiusTopLeft = indicatorStyle.radiusTopRight = 9.0;
			indicatorStyle.radiusBottomLeft = indicatorStyle.radiusBottomRight = 9.0;
			indicatorStyle.childAlignX = LayoutAlignment.Center;
			indicatorStyle.childAlignY = LayoutAlignment.Center;
			var indicator = new RenderNode(context.id("indicator"), LayoutVisualKind.Box,
				indicatorStyle);
			var dotStyle = new LayoutStyle();
			dotStyle.width = LayoutAxis.fixed(8.0);
			dotStyle.height = LayoutAxis.fixed(8.0);
			dotStyle.background = selected ? context.theme.controlSelected :
				Color.rgba(0.0, 0.0, 0.0, 0.0);
			dotStyle.radiusTopLeft = dotStyle.radiusTopRight = 4.0;
			dotStyle.radiusBottomLeft = dotStyle.radiusBottomRight = 4.0;
			indicator.add(new RenderNode(context.id("dot"), LayoutVisualKind.Box, dotStyle));
			node.add(indicator);

			var text = new RenderNode(context.id("label"), LayoutVisualKind.Text);
			text.layout.text = label;
			text.layout.textColor = context.theme.textColor(enabled);
			node.add(text);
			if (enabled && hasSelectHandler) {
				var select = function(_:UiEvent) { onSelect(value); };
				node.on(UiEventKind.Click, select);
				node.on(UiEventKind.Activate, select);
			}
			return node;
		});
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.direction = LayoutDirection.LeftToRight;
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(32.0);
		result.padding = new Insets(4.0, 5.0, 4.0, 5.0);
		result.childGap = 9.0;
		result.childAlignY = LayoutAlignment.Center;
		return result;
	}
}
