package nativekit.ui.widgets;

import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.style.StyleTarget;

/** Stateful tab strip and selected page composed from Haxe buttons and views. */
class Tabs implements View {
	final key:Key;
	public final items:Array<TabItem>;
	public var selectedKey:String;
	public final style:LayoutStyle;
	public var onChange:String->Void;
	public var hasChangeHandler(default, null):Bool;

	public function new(key:String, items:Array<TabItem>, selectedKey:String = "",
			?onChange:String->Void, ?style:LayoutStyle) {
		this.key = new Key(key);
		this.items = items == null ? [] : items.copy();
		this.selectedKey = selectedKey == null ? "" : selectedKey;
		this.style = style == null ? defaultStyle() : style.copy();
		hasChangeHandler = onChange != null;
		this.onChange = onChange == null ? function(_) {} : onChange;
		var keys:Map<String, Bool> = new Map();
		for (item in this.items) {
			if (item == null || keys.exists(item.key))
				throw "Tab keys must be unique";
			keys.set(item.key, true);
		}
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(key, function() {
			var state:State<String> = context.state(context.id("selected-tab"), selectedKey);
			var active:String = state.value;
			if (!isEnabled(active))
				active = firstEnabled();
			if (active != state.value)
				state.update(active);
			selectedKey = active;
			var select = function(next:String) {
				if (next == selectedKey)
					return;
				selectedKey = next;
				state.update(next);
				if (hasChangeHandler)
					onChange(next);
			};

			var rootId = context.id("tabs");
			var rootFlags = context.interactionStates.get(rootId);
			var rootComputed = context.styleResolver.resolve(
				new StyleTarget("tabs", key.value, key.value, null, ["tabs"], rootFlags),
				context.inheritedStyle, context.theme.styles, context.styleSheet, style, context.environment);
			var root = new RenderNode(rootId, LayoutVisualKind.Box, rootComputed.toLayoutStyle());
			root.setStyleIdentity("tabs", key.value, key.value, null, ["tabs"]);
			root.states = rootFlags;
			root.computedStyle = rootComputed;
			var tabsSemantics = new Semantics(AccessibilityRole.TabList, "Tabs");
			tabsSemantics.orientation = AccessibilityOrientation.Horizontal;
			root.semantics = tabsSemantics;
			var stripStyle = new LayoutStyle();
			stripStyle.width = LayoutAxis.grow();
			stripStyle.direction = LayoutDirection.LeftToRight;
			stripStyle.childGap = 4.0;
			var strip = new RenderNode(context.id("tab-strip"), LayoutVisualKind.Box, stripStyle);
			var buttonNodes:Array<RenderNode> = [];
			for (item in items) {
				var button = new Button(item.label, null, function() { select(item.key); }, item.key);
				button.enabled = item.enabled;
				button.selected = item.key == active;
				button.semanticRole = AccessibilityRole.Tab;
				button.semanticActions = AccessibilityAction.Select;
				var buttonNode = context.withScope(new Key(item.key),
					function() return button.build(context));
				strip.add(buttonNode);
				buttonNodes.push(buttonNode);
			}
			for (index in 0...buttonNodes.length) {
				var tabIndex = index;
				buttonNodes[index].on(UiEventKind.KeyDown, function(event) {
					var direction = event.key == UiKey.Right || event.key == UiKey.Down ? 1 :
						event.key == UiKey.Left || event.key == UiKey.Up ? -1 : 0;
					if (direction == 0 || buttonNodes.length == 0)
						return;
					var nextIndex = tabIndex;
					for (_ in 0...buttonNodes.length) {
						nextIndex = (nextIndex + direction + buttonNodes.length) % buttonNodes.length;
						if (items[nextIndex].enabled)
							break;
					}
					if (items[nextIndex].enabled) {
						select(items[nextIndex].key);
						context.requestFocus(buttonNodes[nextIndex].id);
						event.preventDefault();
					}
				});
			}
			root.add(strip);

			var selectedItem:Null<TabItem> = null;
			for (item in items)
				if (item.key == active) {
					selectedItem = item;
					break;
				}
			if (selectedItem != null) {
				var item:TabItem = cast selectedItem;
				var panel = context.withScope(new Key("panel:" + item.key), function() {
					var node = new RenderNode(context.id("tab-panel"), LayoutVisualKind.Box);
					node.semantics = new Semantics(AccessibilityRole.TabPanel, item.label);
					node.add(context.withScope(new Key("content"),
						function() return item.content.build(context)));
					return node;
				});
				root.add(panel);
			}
			return root;
		});
	}

	function isEnabled(key:String):Bool {
		for (item in items)
			if (item.enabled && item.key == key)
				return true;
		return false;
	}

	function firstEnabled():String {
		for (item in items)
			if (item.enabled)
				return item.key;
		return "";
	}

	static function defaultStyle():LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.direction = LayoutDirection.TopToBottom;
		result.childGap = 8.0;
		return result;
	}
}
