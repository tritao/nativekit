package nativekit.ui.widgets;

import Color;
import Insets;
import nativekit.ui.core.Key;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;

/** Compositional Haxe button; the native engine sees only box and text nodes. */
class Button implements View {
	public final label:String;
	public final style:LayoutStyle;
	public var enabled:Bool;
	public var onClick:Void->Void;

	public function new(label:String, ?style:LayoutStyle, ?onClick:Void->Void) {
		this.label = label == null ? "" : label;
		this.style = style == null ? defaultStyle() : style;
		this.onClick = onClick;
		enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		var node = new RenderNode(context.id("button"), LayoutVisualKind.Box, style);
		node.focusable = true;
		node.enabled = enabled;
		var semantics = new Semantics(AccessibilityRole.Button, label);
		semantics.actions = AccessibilityAction.Activate;
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
			text.layout.textColor = Color.rgba(1.0, 1.0, 1.0, 1.0);
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
