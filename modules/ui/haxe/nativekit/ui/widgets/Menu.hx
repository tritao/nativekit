package nativekit.ui.widgets;

import LayoutAxis;
import LayoutStyle;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.View;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;

/** Keyboard-focusable popup menu composed from ordinary Haxe buttons. */
class Menu implements View {
	final key:String;
	final items:Array<MenuItem>;
	public final x:Float;
	public final y:Float;
	public var onDismiss:Void->Void;
	public var hasDismissHandler(default, null):Bool;

	public function new(key:String, items:Array<MenuItem>, x:Float = 0.0, y:Float = 0.0,
			?onDismiss:Void->Void) {
		this.key = key;
		this.items = items == null ? [] : items.copy();
		this.x = x;
		this.y = y;
		hasDismissHandler = onDismiss != null;
		this.onDismiss = onDismiss == null ? function() {} : onDismiss;
	}

	public function build(context:BuildContext):nativekit.ui.core.RenderNode {
		var children:Array<KeyedView> = [];
		for (item in items) {
			var button = new Button(item.label, null, function() {
				if (item.hasSelectHandler)
					item.onSelect();
				if (hasDismissHandler)
					onDismiss();
			}, item.key);
			button.classes = ["menu-item"];
			button.enabled = item.enabled;
			button.semanticRole = AccessibilityRole.MenuItem;
			button.semanticActions = AccessibilityAction.Select;
			children.push(new KeyedView(item.key, button));
		}
		var menuStyle = new LayoutStyle();
		menuStyle.width = LayoutAxis.fixed(220.0);
		menuStyle.childGap = 2.0;
		var content = new Column("menu-items", children, menuStyle);
		var popup = new Popup(key, content, x, y, null,
			hasDismissHandler ? onDismiss : null);
		popup.label = "Menu";
		popup.modal = true;
		popup.dimBackdrop = false;
		var root:RenderNode = popup.build(context);
		var semantics = new Semantics(AccessibilityRole.Menu, "Menu");
		semantics.states |= AccessibilityState.Modal;
		if (hasDismissHandler)
			semantics.actions |= AccessibilityAction.Dismiss;
		root.semantics = semantics;
		if (hasDismissHandler)
			root.on(UiEventKind.Activate, function(event) {
				if (event.target.equals(root.id))
					onDismiss();
			});
		return root;
	}
}
