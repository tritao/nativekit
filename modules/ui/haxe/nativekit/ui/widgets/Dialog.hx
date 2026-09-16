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
			var root = new RenderNode(context.id("dialog"), LayoutVisualKind.Box, rootStyle);
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
			backdropStyle.background = context.theme.overlayBackdrop;
			var backdrop = new RenderNode(context.id("backdrop"), LayoutVisualKind.Box,
				backdropStyle);
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
			var center = new RenderNode(context.id("dialog-center"), LayoutVisualKind.Box,
				centerStyle);
			var panelStyle = new LayoutStyle();
			panelStyle.width = LayoutAxis.fixed(width);
			panelStyle.padding = new Insets(24.0, 24.0, 24.0, 24.0);
			panelStyle.childGap = 16.0;
			panelStyle.background = context.theme.panelBackground;
			panelStyle.radiusTopLeft = panelStyle.radiusTopRight = 8.0;
			panelStyle.radiusBottomLeft = panelStyle.radiusBottomRight = 8.0;
			var panel = new RenderNode(context.id("dialog-panel"), LayoutVisualKind.Box,
				panelStyle);
			if (title.length > 0) {
				var heading = context.withScope(new Key("title"), function() {
					var node = new RenderNode(context.id("heading"), LayoutVisualKind.Text);
					node.layout.text = title;
					node.applyTextStyle(context.resolveTextRole(TextRole.Heading));
					node.semantics = new Semantics(AccessibilityRole.Heading, title);
					return node;
				});
				panel.add(heading);
			}
			var child = context.withScope(new Key("content"), function() return content.build(context));
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
