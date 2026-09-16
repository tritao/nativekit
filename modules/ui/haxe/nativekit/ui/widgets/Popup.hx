package nativekit.ui.widgets;

import Color;
import Insets;
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
	final customStyle:Bool;

	public function new(key:String, child:View, x:Float = 0.0, y:Float = 0.0,
			?style:LayoutStyle, ?onDismiss:Void->Void) {
		if (child == null || !finite(x) || !finite(y))
			throw "Popup requires content and a finite parent-relative position";
		this.key = new Key(key);
		this.child = child;
		this.x = x;
		this.y = y;
		customStyle = style != null;
		this.style = style == null ? defaultPanelStyle() : style.copy();
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
			var root = new RenderNode(context.id("popup-layer"), LayoutVisualKind.Box, rootStyle);
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
			var backdrop = new RenderNode(context.id("backdrop"), LayoutVisualKind.Box,
				backdropStyle);
			if (dismissOnOutside && hasDismissHandler)
				backdrop.on(UiEventKind.Click, function(event) {
				onDismiss();
				event.stopPropagation();
			});
			root.add(backdrop);

			var panelStyle = style.copy();
			if (!customStyle)
				panelStyle.background = context.theme.panelBackground;
			panelStyle.positioning = LayoutPositioning.Absolute;
			panelStyle.positionX = x;
			panelStyle.positionY = y;
			panelStyle.zIndex = 1;
			var panel = new RenderNode(context.id("popup-content"), LayoutVisualKind.Box,
				panelStyle);
			var content = context.withScope(new Key("content"), function() return child.build(context));
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

	static function defaultPanelStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.padding = new Insets(8.0, 8.0, 8.0, 8.0);
		result.background = Color.rgba(0.12, 0.13, 0.16, 1.0);
		result.radiusTopLeft = result.radiusTopRight = 5.0;
		result.radiusBottomLeft = result.radiusBottomRight = 5.0;
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
