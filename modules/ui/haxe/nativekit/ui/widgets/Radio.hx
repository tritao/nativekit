package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDirection;
import LayoutDistribution;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.style.StyleProperty;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.TextRole;

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
			var nodeId = context.id("radio");
			var flags = context.interactionStates.get(nodeId);
			flags = StyleStateUtil.withState(flags, StyleState.Selected, selected);
			flags = StyleStateUtil.withState(flags, StyleState.Disabled, !enabled);
			var computed = context.styleResolver.resolve(
				new StyleTarget("radio", key.value, key.value, null, ["radio"], flags),
				context.inheritedStyle, context.theme.styles, context.styleSheet, style, context.environment);
			var node = new RenderNode(nodeId, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("radio", key.value, key.value, null, ["radio"]);
			node.states = flags;
			node.computedStyle = computed;
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
			indicatorStyle.radiusTopLeft = indicatorStyle.radiusTopRight = 9.0;
			indicatorStyle.radiusBottomLeft = indicatorStyle.radiusBottomRight = 9.0;
			indicatorStyle.childAlignX = LayoutAlignmentX.Center;
			indicatorStyle.childAlignY = LayoutAlignmentY.Center;
			indicatorStyle.childDistribution = LayoutDistribution.Center;
			var indicatorComputed = context.styleResolver.resolve(
				new StyleTarget("radio-indicator", key.value + ":indicator", null,
					null, ["radio"], flags), null, context.theme.styles, context.styleSheet,
				indicatorStyle, context.environment);
			var indicator = new RenderNode(context.id("indicator"), LayoutVisualKind.Box,
				indicatorComputed.toLayoutStyle());
			indicator.setStyleIdentity("radio-indicator", key.value + ":indicator", null,
				null, ["radio"]);
			indicator.states = flags;
			indicator.computedStyle = indicatorComputed;
			var dotStyle = new LayoutStyle();
			dotStyle.width = LayoutAxis.fixed(12.0);
			dotStyle.height = LayoutAxis.fixed(12.0);
			dotStyle.radiusTopLeft = dotStyle.radiusTopRight = 6.0;
			dotStyle.radiusBottomLeft = dotStyle.radiusBottomRight = 6.0;
			dotStyle.childAlignX = LayoutAlignmentX.Center;
			dotStyle.childAlignY = LayoutAlignmentY.Center;
			dotStyle.childDistribution = LayoutDistribution.Center;
			var dotComputed = context.styleResolver.resolve(
				new StyleTarget("radio-dot", key.value + ":dot", null, null, ["radio"], flags),
				null, context.theme.styles, context.styleSheet, dotStyle, context.environment);
			var dot = new RenderNode(context.id("dot"), LayoutVisualKind.Box,
				dotComputed.toLayoutStyle());
			dot.setStyleIdentity("radio-dot", key.value + ":dot", null, null, ["radio"]);
			dot.states = flags;
			dot.computedStyle = dotComputed;
			var markStyle = new LayoutStyle();
			markStyle.width = LayoutAxis.fixed(6.0);
			markStyle.height = LayoutAxis.fixed(6.0);
			markStyle.radiusTopLeft = markStyle.radiusTopRight = 3.0;
			markStyle.radiusBottomLeft = markStyle.radiusBottomRight = 3.0;
			var markComputed = context.styleResolver.resolve(
				new StyleTarget("radio-mark", key.value + ":mark", null, null, ["radio"], flags),
				null, context.theme.styles, context.styleSheet, markStyle, context.environment);
			var mark = new RenderNode(context.id("mark"), LayoutVisualKind.Box,
				markComputed.toLayoutStyle());
			mark.setStyleIdentity("radio-mark", key.value + ":mark", null, null, ["radio"]);
			mark.states = flags;
			mark.computedStyle = markComputed;
			dot.add(mark);
			indicator.add(dot);
			node.add(indicator);

			var text = new RenderNode(context.id("label"), LayoutVisualKind.Text);
			text.layout.text = label;
			text.applyTextStyle(context.resolveTextRole(TextRole.Label).withTextColor(
				context.theme.textRoleColor(TextRole.Label, enabled)));
			var fontSource = computed.source(StyleProperty.FontSize);
			var letterSource = computed.source(StyleProperty.LetterSpacing);
			if (fontSource != null && fontSource.layer != "framework")
				text.layout.textStyle.fontSize = computed.get(StyleProperty.FontSize);
			if (letterSource != null && letterSource.layer != "framework")
				text.layout.textStyle.letterSpacing = computed.get(StyleProperty.LetterSpacing);
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
		result.childAlignY = LayoutAlignmentY.Center;
		return result;
	}
}
