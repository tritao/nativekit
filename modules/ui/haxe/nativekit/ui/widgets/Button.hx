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
import nativekit.ui.core.State;
import nativekit.ui.core.View;
import nativekit.ui.theme.InteractionState;
import nativekit.ui.theme.TextRole;
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
		var interaction:State<Int> = context.state(id, 0);
		var flags:Int = cast interaction.value;
		flags = InteractionState.with(flags, InteractionState.Selected, selected);
		var resolvedStyle = context.theme.resolveButtonStyle(style, flags, enabled);
		var node = new RenderNode(id, LayoutVisualKind.Box, resolvedStyle);
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
		var setState = function(flag:Int, value:Bool) {
			var current:Int = cast interaction.value;
			var next = InteractionState.with(current, flag, value);
			if (next != current)
				interaction.update(next);
		};
		node.on(UiEventKind.HoverEnter, function(_) { if (enabled) setState(InteractionState.Hovered, true); });
		node.on(UiEventKind.HoverLeave, function(_) { setState(InteractionState.Hovered, false); });
		node.on(UiEventKind.PointerDown, function(event) {
			if (enabled && event.button == 0)
				setState(InteractionState.Pressed, true);
		});
		node.on(UiEventKind.PointerUp, function(_) { setState(InteractionState.Pressed, false); });
		node.on(UiEventKind.PointerCancel, function(_) { setState(InteractionState.Pressed, false); });
		node.on(UiEventKind.Focus, function(_) { setState(InteractionState.Focused, true); });
		node.on(UiEventKind.Blur, function(_) { setState(InteractionState.Focused, false); });
		node.on(UiEventKind.FocusLost, function(_) { setState(InteractionState.Focused, false); });
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
		style.background = Color.rgba(0.16, 0.4, 0.78, 1.0);
		style.radiusTopLeft = style.radiusTopRight = 6.0;
		style.radiusBottomLeft = style.radiusBottomRight = 6.0;
		return style;
	}
}
