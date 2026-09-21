package nativekit.ui.widgets;

import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
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
	/** Whether selection is retained locally or supplied by the caller. */
	public var selectionMode(default, null):TabsSelectionMode;
	public final style:LayoutStyle;
	public var onChange:String->Void;
	public var hasChangeHandler(default, null):Bool;
	/** Optional pointer-drag lifecycle used by generic dock workspaces. */
	public var onTabDragStart:Null<String->UiEvent->Void>;
	public var onTabDragMove:Null<String->UiEvent->Void>;
	public var onTabDragEnd:Null<String->UiEvent->Void>;
	public var onTabDragCancel:Null<String->UiEvent->Void>;
	/** Called after each header is built so dock hosts can register geometry. */
	public var onTabHeaderBuilt:Null<String->RenderNode->Void>;

	public function new(key:String, items:Array<TabItem>, selectedKey:String = "",
			?onChange:String->Void, ?style:LayoutStyle,
			?onTabDragStart:String->UiEvent->Void,
			?onTabDragMove:String->UiEvent->Void,
			?onTabDragEnd:String->UiEvent->Void,
			?onTabDragCancel:String->UiEvent->Void,
			?selectionMode:TabsSelectionMode,
			?onTabHeaderBuilt:String->RenderNode->Void) {
		this.key = new Key(key);
		this.items = items == null ? [] : items.copy();
		this.selectedKey = selectedKey == null ? "" : selectedKey;
		this.selectionMode = selectionMode == null ? TabsSelectionMode.Local : selectionMode;
		this.style = style == null ? defaultStyle() : style.copy();
		hasChangeHandler = onChange != null;
		this.onChange = onChange == null ? function(_) {} : onChange;
		this.onTabDragStart = onTabDragStart;
		this.onTabDragMove = onTabDragMove;
		this.onTabDragEnd = onTabDragEnd;
		this.onTabDragCancel = onTabDragCancel;
		this.onTabHeaderBuilt = onTabHeaderBuilt;
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
			var active:String = selectionMode == TabsSelectionMode.Controlled ?
				selectedKey : state.value;
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
			var tabDragState:Null<State<TabDragState>> = null;
			if (onTabDragStart != null || onTabDragMove != null || onTabDragEnd != null ||
				onTabDragCancel != null)
				tabDragState = context.state(context.id("tab-drag"), new TabDragState());
			for (item in items) {
				var button = new Button(item.label, null, function() { select(item.key); }, item.key);
				button.enabled = item.enabled;
				button.selected = item.key == active;
				button.semanticRole = AccessibilityRole.Tab;
				button.semanticActions = AccessibilityAction.Select;
				var buttonNode = context.withStyleParent(rootComputed, function() {
					return context.withScope(new Key(item.key),
						function() return button.build(context));
				});
				strip.add(buttonNode);
				buttonNodes.push(buttonNode);
				if (onTabHeaderBuilt != null)
					onTabHeaderBuilt(item.key, buttonNode);
				installTabDragHandlers(buttonNode, item.key, tabDragState);
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
					node.add(context.withStyleParent(rootComputed, function() {
					return context.withScope(new Key("content"),
						function() return item.content.build(context));
				}));
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

	function installTabDragHandlers(buttonNode:RenderNode, tabKey:String,
			dragState:Null<State<TabDragState>>):Void {
		if (dragState == null)
			return;
		buttonNode.on(UiEventKind.PointerDown, function(event) {
			if (event.button != 0)
				return;
			var drag = dragState.value;
			drag.active = true;
			drag.dragging = false;
			drag.pointerId = event.pointerId;
			drag.tabKey = tabKey;
			drag.startX = event.x;
			drag.startY = event.y;
			dragState.update(drag);
			event.capturePointer();
		});
		buttonNode.on(UiEventKind.PointerMove, function(event) {
			var drag = dragState.value;
			if (!drag.active || drag.pointerId != event.pointerId || drag.tabKey != tabKey)
				return;
			var dx = event.x - drag.startX;
			var dy = event.y - drag.startY;
			if (!drag.dragging && dx * dx + dy * dy >= 36.0) {
				drag.dragging = true;
				dragState.update(drag);
				if (onTabDragStart != null)
					onTabDragStart(tabKey, event);
			}
			if (drag.dragging) {
				if (onTabDragMove != null)
					onTabDragMove(tabKey, event);
				event.preventDefault();
			}
		});
		buttonNode.on(UiEventKind.PointerUp, function(event) {
			var drag = dragState.value;
			if (!drag.active || drag.pointerId != event.pointerId || drag.tabKey != tabKey)
				return;
			if (drag.dragging) {
				if (onTabDragEnd != null)
					onTabDragEnd(tabKey, event);
				event.preventDefault();
			}
			drag.clear();
			dragState.update(drag);
			event.releasePointer();
		});
		buttonNode.on(UiEventKind.PointerCancel, function(event) {
			var drag = dragState.value;
			if (!drag.active || drag.pointerId != event.pointerId || drag.tabKey != tabKey)
				return;
			if (drag.dragging && onTabDragCancel != null)
				onTabDragCancel(tabKey, event);
			drag.clear();
			dragState.update(drag);
			event.releasePointer();
		});
	}
}

private class TabDragState {
	public var active:Bool;
	public var dragging:Bool;
	public var pointerId:Int;
	public var tabKey:String;
	public var startX:Float;
	public var startY:Float;

	public function new() {
		clear();
	}

	public function clear():Void {
		active = false;
		dragging = false;
		pointerId = -1;
		tabKey = "";
		startX = 0.0;
		startY = 0.0;
	}
}
