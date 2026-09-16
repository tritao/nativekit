package nativekit.ui.widgets;

import Color;
import Insets;
import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDistribution;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.style.StyleTarget;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.theme.TextRole;

/** Centered modal composition with trapped keyboard focus and dismissal hooks. */
class Dialog implements View {
	final key:Key;
	final content:View;
	public final title:String;
	public final width:Float;
	public var dismissOnOutside:Bool;
	public var dismissOnEscape:Bool;
	public var onDismiss:Void->Void;
	public var hasDismissHandler(default, null):Bool;

	public function new(key:String, title:String, content:View,
			?onDismiss:Void->Void, width:Float = 440.0) {
		if (content == null || !finite(width) || width <= 0.0)
			throw "Dialog requires content and a positive finite width";
		this.key = new Key(key);
		this.title = title == null ? "" : title;
		this.content = content;
		this.width = width;
		hasDismissHandler = onDismiss != null;
		this.onDismiss = onDismiss == null ? function() {} : onDismiss;
		dismissOnOutside = true;
		dismissOnEscape = true;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var rootStyle = new LayoutStyle();
			rootStyle.width = LayoutAxis.grow();
			rootStyle.height = LayoutAxis.grow();
			var rootId = context.id("dialog");
			var rootComputed = context.resolveStyle(
				new StyleTarget("dialog", key.value, key.value, null, ["dialog"],
					context.interactionStates.get(rootId)), rootStyle);
			var root = new RenderNode(rootId, LayoutVisualKind.Box, rootComputed.toLayoutStyle());
			root.setStyleIdentity("dialog", key.value, key.value, null, ["dialog"]);
			root.states = context.interactionStates.get(rootId);
			root.computedStyle = rootComputed;
			root.focusTrap = true;
			root.hitTestSelf = false;
			var semantics = new Semantics(AccessibilityRole.Dialog, title);
			semantics.states |= AccessibilityState.Modal;
			if (hasDismissHandler)
				semantics.actions |= AccessibilityAction.Dismiss;
			root.semantics = semantics;

			var backdropStyle = new LayoutStyle();
			backdropStyle.width = LayoutAxis.grow();
			backdropStyle.height = LayoutAxis.grow();
			backdropStyle.positioning = LayoutPositioning.Absolute;
			var backdropId = context.id("backdrop");
			var backdropComputed = context.resolveStyle(
				new StyleTarget("dialog-backdrop", "backdrop", "backdrop", null, ["dialog"],
					context.interactionStates.get(backdropId)), backdropStyle);
			var backdrop = new RenderNode(backdropId, LayoutVisualKind.Box,
				backdropComputed.toLayoutStyle());
			backdrop.setStyleIdentity("dialog-backdrop", "backdrop", "backdrop", null, ["dialog"]);
			backdrop.states = context.interactionStates.get(backdropId);
			backdrop.computedStyle = backdropComputed;
			if (dismissOnOutside && hasDismissHandler)
				backdrop.on(UiEventKind.Click, function(event) {
				onDismiss();
				event.stopPropagation();
			});
			root.add(backdrop);

			var centerStyle = new LayoutStyle();
			centerStyle.width = LayoutAxis.grow();
			centerStyle.height = LayoutAxis.grow();
			centerStyle.positioning = LayoutPositioning.Absolute;
			centerStyle.zIndex = 1;
			centerStyle.childAlignX = LayoutAlignmentX.Center;
			centerStyle.childAlignY = LayoutAlignmentY.Center;
			centerStyle.childDistribution = LayoutDistribution.Center;
			var centerId = context.id("dialog-center");
			var centerComputed = context.resolveStyle(
				new StyleTarget("dialog-center", "center", "center", null, ["dialog"],
					context.interactionStates.get(centerId)), centerStyle);
			var center = new RenderNode(centerId, LayoutVisualKind.Box,
				centerComputed.toLayoutStyle());
			center.setStyleIdentity("dialog-center", "center", "center", null, ["dialog"]);
			center.states = context.interactionStates.get(centerId);
			center.computedStyle = centerComputed;
			var panelStyle = new LayoutStyle();
			panelStyle.width = LayoutAxis.fixed(width);
			var panelId = context.id("dialog-panel");
			var panelComputed = context.resolveStyle(
				new StyleTarget("dialog-panel", "panel", "panel", null, ["dialog"],
					context.interactionStates.get(panelId)), panelStyle);
			var panel = new RenderNode(panelId, LayoutVisualKind.Box,
				panelComputed.toLayoutStyle());
			panel.setStyleIdentity("dialog-panel", "panel", "panel", null, ["dialog"]);
			panel.states = context.interactionStates.get(panelId);
			panel.computedStyle = panelComputed;
			if (title.length > 0) {
				var heading = context.withStyleParent(panelComputed, function() return
					context.withScope(new Key("title"), function() {
					var headingId = context.id("heading");
					var headingComputed = context.resolveStyle(
						new StyleTarget("dialog-heading", "title", "title", null, ["dialog-heading"],
							context.interactionStates.get(headingId)), new LayoutStyle());
					var node = new RenderNode(headingId, LayoutVisualKind.Text,
						headingComputed.toLayoutStyle());
					node.setStyleIdentity("dialog-heading", "title", "title", null, ["dialog-heading"]);
					node.states = context.interactionStates.get(headingId);
					node.computedStyle = headingComputed;
					node.layout.text = title;
					node.layout.textColor = headingComputed.get(nativekit.ui.style.StyleProperty.TextColor);
					node.layout.textStyle.fontSize = headingComputed.get(nativekit.ui.style.StyleProperty.FontSize);
					node.layout.textStyle.letterSpacing = headingComputed.get(nativekit.ui.style.StyleProperty.LetterSpacing);
					node.semantics = new Semantics(AccessibilityRole.Heading, title);
					return node;
				}));
				panel.add(heading);
			}
			var child = context.withStyleParent(panelComputed, function() return
				context.withScope(new Key("content"), function() return content.build(context)));
			panel.add(child);
			center.add(panel);
			root.add(center);
			root.on(UiEventKind.KeyDown, function(event) {
				if (event.key == UiKey.Escape && dismissOnEscape && hasDismissHandler) {
					event.preventDefault();
					onDismiss();
				}
			});
			if (hasDismissHandler)
				root.on(UiEventKind.Activate, function(event) {
					if (event.target.equals(root.id))
						onDismiss();
				});
			return root;
		});
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
