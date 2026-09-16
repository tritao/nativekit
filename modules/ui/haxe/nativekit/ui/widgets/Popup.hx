package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutPositioning;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.style.StyleTarget;

/** Parent-sized popup layer with optional modal focus and outside-click dismissal. */
class Popup implements View {
	final key:Key;
	final child:View;
	public final x:Float;
	public final y:Float;
	public final style:LayoutStyle;
	public var label:Null<String>;
	public var modal:Bool;
	public var dimBackdrop:Bool;
	public var dismissOnOutside:Bool;
	public var dismissOnEscape:Bool;
	public var backdropColor:Null<Color>;
	public var onDismiss:Void->Void;
	public var hasDismissHandler(default, null):Bool;
	public function new(key:String, child:View, x:Float = 0.0, y:Float = 0.0,
			?style:LayoutStyle, ?onDismiss:Void->Void) {
		if (child == null || !finite(x) || !finite(y))
			throw "Popup requires content and a finite parent-relative position";
		this.key = new Key(key);
		this.child = child;
		this.x = x;
		this.y = y;
		this.style = style == null ? new LayoutStyle() : style.copy();
		label = null;
		modal = true;
		dimBackdrop = true;
		dismissOnOutside = true;
		dismissOnEscape = true;
		backdropColor = null;
		hasDismissHandler = onDismiss != null;
		this.onDismiss = onDismiss == null ? function() {} : onDismiss;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var rootStyle = new LayoutStyle();
			rootStyle.width = LayoutAxis.grow();
			rootStyle.height = LayoutAxis.grow();
			var rootId = context.id("popup-layer");
			var rootComputed = context.resolveStyle(
				new StyleTarget("popup", key.value, key.value, null, ["popup"],
					context.interactionStates.get(rootId)), rootStyle);
			var root = new RenderNode(rootId, LayoutVisualKind.Box, rootComputed.toLayoutStyle());
			root.setStyleIdentity("popup", key.value, key.value, null, ["popup"]);
			root.states = context.interactionStates.get(rootId);
			root.computedStyle = rootComputed;
			root.hitTestSelf = false;
			root.focusTrap = modal;
			if (label != null)
				root.semantics = new Semantics(AccessibilityRole.Group, label);

			var backdropStyle = new LayoutStyle();
			backdropStyle.width = LayoutAxis.grow();
			backdropStyle.height = LayoutAxis.grow();
			backdropStyle.positioning = LayoutPositioning.Absolute;
			backdropStyle.visible = modal || dismissOnOutside;
			var color = backdropColor == null ? context.theme.overlayBackdrop : cast backdropColor;
			backdropStyle.background = modal && dimBackdrop
				? color : Color.rgba(0.0, 0.0, 0.0, 0.0);
			var backdropId = context.id("backdrop");
			var backdropComputed = context.resolveStyle(
				new StyleTarget("popup-backdrop", "backdrop", "backdrop", null, ["popup"],
					context.interactionStates.get(backdropId)), backdropStyle);
			var backdrop = new RenderNode(backdropId, LayoutVisualKind.Box,
				backdropComputed.toLayoutStyle());
			backdrop.setStyleIdentity("popup-backdrop", "backdrop", "backdrop", null, ["popup"]);
			backdrop.states = context.interactionStates.get(backdropId);
			backdrop.computedStyle = backdropComputed;
			if (dismissOnOutside && hasDismissHandler)
				backdrop.on(UiEventKind.Click, function(event) {
				onDismiss();
				event.stopPropagation();
			});
			root.add(backdrop);

			var panelStyle = style.copy();
			panelStyle.positioning = LayoutPositioning.Absolute;
			panelStyle.positionX = x;
			panelStyle.positionY = y;
			panelStyle.zIndex = 1;
			var panelId = context.id("popup-content");
			var panelComputed = context.resolveStyle(
				new StyleTarget("popup-content", key.value, key.value, null, ["popup"],
					context.interactionStates.get(panelId)), panelStyle);
			var panel = new RenderNode(panelId, LayoutVisualKind.Box,
				panelComputed.toLayoutStyle());
			panel.setStyleIdentity("popup-content", key.value, key.value, null, ["popup"]);
			panel.states = context.interactionStates.get(panelId);
			panel.computedStyle = panelComputed;
			var content = context.withStyleParent(panelComputed, function() return
				context.withScope(new Key("content"), function() return child.build(context)));
			panel.add(content);
			root.add(panel);
			root.on(UiEventKind.KeyDown, function(event) {
				if (event.key == UiKey.Escape && dismissOnEscape && hasDismissHandler) {
					event.preventDefault();
					onDismiss();
				}
			});
			return root;
		});
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
