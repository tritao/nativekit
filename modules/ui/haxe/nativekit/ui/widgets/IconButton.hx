package nativekit.ui.widgets;

import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.View;
import nativekit.ui.icons.IconName;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleState;
import nativekit.ui.style.StyleStateUtil;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.widgets.Icon;

/** Accessible icon-only button with a required non-visual label. */
class IconButton implements View {
	public final key:String;
	public final icon:IconName;
	public final label:String;
	public final style:LayoutStyle;
	public var enabled:Bool;
	public var onClick:Void->Void;

	public function new(key:String, icon:IconName, label:String,
			?onClick:Void->Void, ?style:LayoutStyle) {
		if (key == null || key.length == 0 || label == null || label.length == 0)
			throw "Icon buttons require a stable key and accessible label";
		this.key = key;
		this.icon = icon;
		this.label = label;
		this.onClick = onClick;
		this.style = style == null ? new LayoutStyle() : style.copy();
		enabled = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var id = context.id("icon-button");
			var flags = StyleStateUtil.withState(context.interactionStates.get(id),
				StyleState.Disabled, !enabled);
			var computed = context.resolveStyle(new StyleTarget("button", key, key,
				["icon-button"], ["button", "icon-button"], flags), style);
			var node = new RenderNode(id, LayoutVisualKind.Box, computed.toLayoutStyle());
			node.setStyleIdentity("button", key, key, ["icon-button"],
				["button", "icon-button"]);
			node.states = flags;
			node.computedStyle = computed;
			node.focusable = enabled;
			node.enabled = enabled;
			var semantics = new Semantics(AccessibilityRole.Button, label);
			semantics.actions = AccessibilityAction.Activate;
			if (!enabled)
				semantics.states |= AccessibilityState.Disabled;
			node.semantics = semantics;
			if (enabled && onClick != null) {
				var activate = function(_:UiEvent) {
					onClick();
				};
				node.on(UiEventKind.Click, activate);
				node.on(UiEventKind.Activate, activate);
			}
			var foreground = context.theme.buttonLabelColor(enabled,
				node.layout.style.background);
			node.add(new Icon("glyph", icon, 16.0, foreground).build(context));
			return node;
		});
	}
}
