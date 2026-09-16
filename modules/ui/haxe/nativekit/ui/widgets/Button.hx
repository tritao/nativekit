package nativekit.ui.widgets;

import Color;
import Insets;
import nativekit.ui.core.Key;
import LayoutStyle;
import LayoutVisualKind;
import TextWrap;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.theme.TextRole;
import nativekit.ui.style.StyleResolver;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Compositional Haxe button; the native engine sees only box and text nodes. */
class Button implements View {
	public final key:String;
	public final label:String;
	public final style:LayoutStyle;
	public var enabled:Bool;
	public var selected:Bool;
	/** Semantic role override used by composite controls such as tabs and menus. */
	public var semanticRole:AccessibilityRole;
	/** Action capabilities override used by composite controls. */
	public var semanticActions:Int;
	public var onClick:Void->Void;

	public function new(label:String, ?style:LayoutStyle, ?onClick:Void->Void, ?key:String) {
		this.label = label == null ? "" : label;
		this.key = key == null || key.length == 0
			? (this.label.length == 0 ? "button" : this.label)
			: key;
		this.style = style == null ? defaultStyle() : style.copy();
		this.onClick = onClick;
		enabled = true;
		selected = false;
		semanticRole = AccessibilityRole.Button;
		semanticActions = AccessibilityAction.Activate;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() return buildScoped(context));
	}

	function buildScoped(context:BuildContext):RenderNode {
		var id = context.id("button");
		var flags:Int = context.interactionStates.get(id);
		flags = StyleStateUtil.withState(flags, StyleState.Selected, selected);
		flags = StyleStateUtil.withState(flags, StyleState.Disabled, !enabled);
		var target = new StyleTarget("button", key, key, null, ["button"], flags);
		var computed = context.styleResolver.resolve(target, null, context.theme.styles,
			context.styleSheet, style, context.environment);
		var resolvedStyle = computed.toLayoutStyle();
		var node = new RenderNode(id, LayoutVisualKind.Box, resolvedStyle);
		node.setStyleIdentity("button", key, key, null, ["button"]);
		node.states = flags;
		node.computedStyle = computed;
		node.focusable = enabled;
		node.enabled = enabled;
		var semantics = new Semantics(semanticRole, label);
		semantics.actions = semanticActions;
		if (selected)
			semantics.states |= AccessibilityState.Selected;
		node.semantics = semantics;
		if (enabled && onClick != null) {
			var activate = function(_:UiEvent) {
				onClick();
			};
			node.on(UiEventKind.Click, activate);
			node.on(UiEventKind.Activate, activate);
		}
		var labelNode = context.withScope(new Key("label"), function() {
			var text = new RenderNode(context.id("label"), LayoutVisualKind.Text);
			text.layout.text = label;
			var labelStyle = context.resolveTextRole(TextRole.Button,
				TextStyleOverride.paragraph(TextWrap.None));
			labelStyle = labelStyle.withTextColor(
				context.theme.buttonLabelColor(enabled, resolvedStyle.background));
			text.applyTextStyle(labelStyle);
			return text;
		});
		node.add(labelNode);
		return node;
	}

	static function defaultStyle():LayoutStyle {
		var style = new LayoutStyle();
		style.padding = new Insets(12.0, 12.0, 8.0, 8.0);
		style.radiusTopLeft = style.radiusTopRight = 6.0;
		style.radiusBottomLeft = style.radiusBottomRight = 6.0;
		return style;
	}
}
