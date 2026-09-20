package nativekit.ui.widgets;

import Color;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import LayoutVisualKind;
import Insets;
import nativekit.ui.core.BuildContext;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.State;
import nativekit.ui.core.UiEvent;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.View;
import nativekit.ui.core.WidgetId;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollController;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.Spacer;
import nativekit.ui.widgets.Text;

/** Model-backed virtual tree with stable expansion and selection state. */
class TreeView implements View {
	public final key:String;
	public final model:TreeViewModel;
	public final viewportStyle:LayoutStyle;
	public var controller(default, null):ScrollController;
	public var selectedKey(default, null):Null<String>;
	public var onSelectionChanged:Null<String->Void>;
	public var onItemActivated:Null<String->Void>;
	public var onExpandedChanged:Null<String->Bool->Void>;

	final fallbackViewportHeight:Float;
	final expandedKeys:Map<String, Bool>;
	var expandedState:Null<State<Map<String, Bool>>>;
	var selectedState:Null<State<String>>;
	var expansionRevision:Int;
	var cachedModelRevision:Int;
	var cachedExpansionRevision:Int;
	var entries:Array<TreeEntry>;
	var indexByKey:Map<String, Int>;
	var entryByKey:Map<String, TreeEntry>;
	var cachedExtents:Array<Float>;
	var extentViewport:Null<VirtualExtentViewport>;
	var itemIds:Map<String, WidgetId>;

	public function new(key:String, model:TreeViewModel, ?viewportStyle:LayoutStyle,
			?controller:ScrollController, viewportHeight:Float = 300.0,
			?selectedKey:String, ?expandedKeys:Array<String>,
			?onSelectionChanged:String->Void, ?onItemActivated:String->Void,
			?onExpandedChanged:String->Bool->Void) {
		if (key == null || key.length == 0 || model == null || viewportHeight <= 0.0 ||
			!finite(viewportHeight) || (selectedKey != null && selectedKey.length == 0))
			throw "TreeView requires a stable key, model, and valid viewport height";
		this.key = key;
		this.model = model;
		this.viewportStyle = viewportStyle == null ? defaultViewportStyle(viewportHeight) :
			viewportStyle.copy();
		this.controller = controller == null ? new ScrollController() : controller;
		this.fallbackViewportHeight = viewportHeight;
		this.selectedKey = selectedKey;
		this.onSelectionChanged = onSelectionChanged;
		this.onItemActivated = onItemActivated;
		this.onExpandedChanged = onExpandedChanged;
		this.expandedKeys = new Map();
		if (expandedKeys != null)
			for (expandedKey in expandedKeys) {
				if (expandedKey == null || expandedKey.length == 0)
					throw "TreeView expanded keys must be non-empty";
				this.expandedKeys.set(expandedKey, true);
			}
		expandedState = null;
		selectedState = null;
		expansionRevision = 0;
		cachedModelRevision = -1;
		cachedExpansionRevision = -1;
		entries = [];
		indexByKey = new Map();
		entryByKey = new Map();
		cachedExtents = [];
		extentViewport = null;
		itemIds = new Map();
	}

	/** Selects a visible node. Passing null clears selection without a callback. */
	public function select(nextKey:Null<String>):Bool {
		ensureTreeMetrics();
		if (nextKey != null && (nextKey.length == 0 || !indexByKey.exists(nextKey)))
			throw "TreeView selection key is not visible";
		if (nextKey == selectedKey)
			return false;
		selectedKey = nextKey;
		updateSelectedState(nextKey);
		if (nextKey != null && onSelectionChanged != null)
			onSelectionChanged(nextKey);
		return true;
	}

	/** Expands or collapses a visible node. */
	public function setExpanded(nodeKey:String, expanded:Bool):Bool {
		ensureTreeMetrics();
		var entry = entryByKey.get(nodeKey);
		if (entry == null)
			throw "TreeView expansion key is not visible";
		if (!entry.hasChildren || entry.expanded == expanded)
			return false;
		var next = copyExpanded(expandedKeys);
		next.set(nodeKey, expanded);
		expandedKeys.clear();
		for (key in next.keys()) {
			var value:Bool = cast next.get(key);
			expandedKeys.set(key, value);
		}
		if (expandedState != null) {
			var state:State<Map<String, Bool>> = cast expandedState;
			state.update(expandedKeys);
		}
		expansionRevision++;
		cachedExpansionRevision = -1;
		if (onExpandedChanged != null)
			onExpandedChanged(nodeKey, expanded);
		return true;
	}

	public function toggleExpanded(nodeKey:String):Bool {
		ensureTreeMetrics();
		var entry = entryByKey.get(nodeKey);
		if (entry == null)
			throw "TreeView expansion key is not visible";
		return setExpanded(nodeKey, !entry.expanded);
	}

	public function isExpanded(nodeKey:String):Bool {
		ensureTreeMetrics();
		var entry = entryByKey.get(nodeKey);
		return entry != null && entry.expanded;
	}

	/** Scrolls a visible node to the top of the viewport. */
	public function scrollTo(nodeKey:String):Bool {
		ensureTreeMetrics();
		var index = indexByKey.get(nodeKey);
		if (index == null)
			throw "TreeView scroll key is not visible";
		return controller.jumpTo(controller.offsetX, extentOffset(index));
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var expansion:State<Map<String, Bool>> = context.state(
				context.id("expanded-state"), expandedKeys);
			expandedState = expansion;
			if (expansion.value != expandedKeys) {
				expandedKeys.clear();
				for (storedKey in expansion.value.keys()) {
					var value:Bool = cast expansion.value.get(storedKey);
					expandedKeys.set(storedKey, value);
				}
				expansionRevision++;
			}
			var selection:State<String> = context.state(context.id("selected-key"),
				selectedKey == null ? "" : selectedKey);
			selectedState = selection;
			selectedKey = selection.value == "" ? null : selection.value;
			ensureTreeMetrics();
			if (selectedKey != null && !indexByKey.exists(selectedKey)) {
				selectedKey = null;
				selection.update("");
			}
			itemIds = new Map();

			var viewportHeight = controller.viewportHeight > 0.0 ? controller.viewportHeight :
				(viewportStyle.height.sizing == LayoutSizing.Fixed ? viewportStyle.height.value :
				fallbackViewportHeight);
			var window:VirtualExtentViewport = cast extentViewport;
			window.update(viewportHeight, controller.offsetY);
			var rowViews:Array<KeyedView> = [];
			if (window.count > 0) {
				rowViews.push(new KeyedView("before", new Spacer("before-spacer",
					LayoutAxis.grow(), LayoutAxis.fixed(window.startOffset(window.first)))));
				for (index in window.first...window.last) {
					var entry = entries[index];
					var nodeKey = entry.key;
					var item = model.buildItem(nodeKey);
					if (item == null)
						throw 'TreeView model returned null for key $nodeKey';
					var row = new TreeViewRow("row", item, entry, selectedKey == nodeKey,
						function() { select(nodeKey); },
						function() { if (onItemActivated != null) onItemActivated(nodeKey); },
						function() { toggleExpanded(nodeKey); },
						function(event) { handleNodeKey(context, entry, event); },
						function(id) { itemIds.set(nodeKey, id); });
					rowViews.push(new KeyedView('item:$nodeKey', row));
				}
				rowViews.push(new KeyedView("after", new Spacer("after-spacer",
					LayoutAxis.grow(), LayoutAxis.fixed(window.totalExtent -
					window.startOffset(window.last)))));
			}

			var contentStyle = new LayoutStyle();
			contentStyle.width = LayoutAxis.grow();
			contentStyle.height = LayoutAxis.fixed(window.totalExtent);
			var content = new Column("tree-content", rowViews, contentStyle);
			var scroll = new ScrollView("viewport", content, viewportStyle,
				ScrollAxis.Vertical, controller);
			var root = new RenderNode(context.id("tree"), LayoutVisualKind.Box);
			root.layout.style.width = viewportStyle.width;
			root.layout.style.height = viewportStyle.height;
			var semantics = new Semantics(AccessibilityRole.Tree);
			semantics.setSize = entries.length;
			semantics.orientation = AccessibilityOrientation.Vertical;
			semantics.actions = AccessibilityAction.ScrollForward | AccessibilityAction.ScrollBackward;
			root.semantics = semantics;
			var viewport = context.withScope(new Key("scroll-view"), function() return scroll.build(context));
			root.add(viewport);
			return root;
		});
	}

	function handleNodeKey(context:BuildContext, entry:TreeEntry, event:UiEvent):Void {
		var nextKey:Null<String> = null;
		switch (event.key) {
			case UiKey.Up: nextKey = adjacentKey(entry.key, -1);
			case UiKey.Down: nextKey = adjacentKey(entry.key, 1);
			case UiKey.Home: nextKey = entries.length == 0 ? null : entries[0].key;
			case UiKey.End: nextKey = entries.length == 0 ? null : entries[entries.length - 1].key;
			case UiKey.PageUp: nextKey = adjacentKey(entry.key, -visibleNodeCount());
			case UiKey.PageDown: nextKey = adjacentKey(entry.key, visibleNodeCount());
			case UiKey.Left:
				if (entry.hasChildren && entry.expanded) {
					event.preventDefault();
					setExpanded(entry.key, false);
					return;
				}
				if (entry.parentKey != null)
					nextKey = entry.parentKey;
			case UiKey.Right:
				if (entry.hasChildren && !entry.expanded) {
					event.preventDefault();
					setExpanded(entry.key, true);
					return;
				}
				if (entry.expanded && entry.hasChildren)
					nextKey = firstChildKey(entry.key);
			default: return;
		}
		if (nextKey == null || nextKey == entry.key)
			return;
		event.preventDefault();
		select(nextKey);
		scrollTo(nextKey);
		var target = itemIds.get(nextKey);
		if (target != null)
			context.requestFocus(target);
	}

	function adjacentKey(nodeKey:String, delta:Int):Null<String> {
		var current = indexByKey.get(nodeKey);
		if (current == null || entries.length == 0)
			return null;
		var next = Std.int(Math.max(0, Math.min(entries.length - 1, current + delta)));
		return entries[next].key;
	}

	function firstChildKey(parentKey:String):Null<String> {
		var parent = indexByKey.get(parentKey);
		if (parent == null || parent + 1 >= entries.length)
			return null;
		var child = entries[parent + 1];
		return child.parentKey == parentKey ? child.key : null;
	}

	function visibleNodeCount():Int {
		if (extentViewport == null)
			return 1;
		var window:VirtualExtentViewport = cast extentViewport;
		if (window.count == 0)
			return 1;
		return Std.int(Math.max(1.0, Math.ceil(window.viewportExtent /
			Math.max(1.0, cachedExtents[window.first]))));
	}

	function ensureTreeMetrics():Void {
		var modelRevision = model.revision();
		if (cachedModelRevision == modelRevision && cachedExpansionRevision == expansionRevision &&
			extentViewport != null)
			return;
		var flattened:Array<TreeEntry> = [];
		var pending:Array<TreePending> = [];
		var roots:Array<String> = [];
		var rootCount = model.rootCount();
		if (rootCount < 0)
			throw "TreeView root count must be non-negative";
		for (index in 0...rootCount) {
			var rootKey = model.rootKeyAt(index);
			if (rootKey == null || rootKey.length == 0)
				throw 'TreeView root $index has an empty key';
			roots.push(rootKey);
		}
		for (index in 0...roots.length)
			pending.push(new TreePending(roots[roots.length - index - 1], null, 0));

		var visibleKeys:Map<String, Bool> = new Map();
		while (pending.length > 0) {
			var next = pending.pop();
			if (visibleKeys.exists(next.key))
				throw 'TreeView contains a duplicate visible key ${next.key}';
			visibleKeys.set(next.key, true);
			var childCount = model.childCount(next.key);
			if (childCount < 0)
				throw 'TreeView child count for ${next.key} must be non-negative';
			var expanded = childCount > 0 && expansionFor(next.key);
			var extent = model.extentAt(next.key);
			if (extent <= 0.0 || !finite(extent))
				throw 'TreeView extent for ${next.key} must be finite and positive';
			var entry = new TreeEntry(next.key, next.parentKey, next.depth,
				childCount > 0, expanded, extent);
			flattened.push(entry);
			if (!expanded)
				continue;
			var children:Array<String> = [];
			for (childIndex in 0...childCount) {
				var childKey = model.childKeyAt(next.key, childIndex);
				if (childKey == null || childKey.length == 0)
					throw 'TreeView child ${next.key}:$childIndex has an empty key';
				children.push(childKey);
			}
			for (childIndex in 0...children.length)
				pending.push(new TreePending(children[children.length - childIndex - 1],
					next.key, next.depth + 1));
		}

		entries = flattened;
		indexByKey = new Map();
		entryByKey = new Map();
		cachedExtents = [];
		for (index in 0...entries.length) {
			indexByKey.set(entries[index].key, index);
			entryByKey.set(entries[index].key, entries[index]);
			cachedExtents.push(entries[index].extent);
		}
		cachedModelRevision = modelRevision;
		cachedExpansionRevision = expansionRevision;
		extentViewport = new VirtualExtentViewport(cachedExtents,
			fallbackViewportHeight, controller.offsetY);
		if (selectedKey != null && !indexByKey.exists(selectedKey)) {
			selectedKey = null;
			updateSelectedState(null);
		}
	}

	function expansionFor(nodeKey:String):Bool {
		if (expandedKeys.exists(nodeKey))
			return expandedKeys.get(nodeKey);
		var expanded = model.initiallyExpanded(nodeKey);
		expandedKeys.set(nodeKey, expanded);
		return expanded;
	}

	function updateSelectedState(nextKey:Null<String>):Void {
		if (selectedState == null)
			return;
		var state:State<String> = cast selectedState;
		state.update(nextKey == null ? "" : nextKey);
	}

	function extentOffset(index:Int):Float {
		if (extentViewport == null)
			return 0.0;
		var window:VirtualExtentViewport = cast extentViewport;
		return window.startOffset(index);
	}

	static function copyExpanded(source:Map<String, Bool>):Map<String, Bool> {
		var result:Map<String, Bool> = new Map();
		for (key in source.keys()) {
			var value:Bool = cast source.get(key);
			result.set(key, value);
		}
		return result;
	}

	static function defaultViewportStyle(height:Float):LayoutStyle {
		var result = new LayoutStyle();
		result.width = LayoutAxis.grow();
		result.height = LayoutAxis.fixed(height);
		return result;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}

private class TreeEntry {
	public final key:String;
	public final parentKey:Null<String>;
	public final depth:Int;
	public final hasChildren:Bool;
	public final expanded:Bool;
	public final extent:Float;

	public function new(key:String, parentKey:Null<String>, depth:Int,
			hasChildren:Bool, expanded:Bool, extent:Float) {
		this.key = key;
		this.parentKey = parentKey;
		this.depth = depth;
		this.hasChildren = hasChildren;
		this.expanded = expanded;
		this.extent = extent;
	}
}

private class TreePending {
	public final key:String;
	public final parentKey:Null<String>;
	public final depth:Int;

	public function new(key:String, parentKey:Null<String>, depth:Int) {
		this.key = key;
		this.parentKey = parentKey;
		this.depth = depth;
	}
}

private class TreeViewRow implements View {
	final key:String;
	final child:View;
	final entry:TreeEntry;
	final selected:Bool;
	final onSelect:Void->Void;
	final onActivate:Void->Void;
	final onToggle:Void->Void;
	final onKey:UiEvent->Void;
	final onBuilt:WidgetId->Void;

	public function new(key:String, child:View, entry:TreeEntry, selected:Bool,
			onSelect:Void->Void, onActivate:Void->Void, onToggle:Void->Void,
			onKey:UiEvent->Void, onBuilt:WidgetId->Void) {
		this.key = key;
		this.child = child;
		this.entry = entry;
		this.selected = selected;
		this.onSelect = onSelect;
		this.onActivate = onActivate;
		this.onToggle = onToggle;
		this.onKey = onKey;
		this.onBuilt = onBuilt;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.grow();
			style.height = LayoutAxis.fixed(entry.extent);
			style.direction = LayoutDirection.LeftToRight;
			style.padding = new Insets(entry.depth * 16.0, 0.0, 0.0, 0.0);
			style.background = selected ? context.theme.tokens.selectionHighlight :
				Color.rgba(0.0, 0.0, 0.0, 0.0);
			var node = new RenderNode(context.id("tree-item"), LayoutVisualKind.Box, style);
			node.focusable = true;
			var semantics = new Semantics(AccessibilityRole.TreeItem);
			semantics.actions = AccessibilityAction.Activate | AccessibilityAction.Select;
			if (entry.hasChildren)
				semantics.actions |= entry.expanded ? AccessibilityAction.Collapse :
					AccessibilityAction.Expand;
			semantics.states |= AccessibilityState.Focusable;
			if (selected)
				semantics.states |= AccessibilityState.Selected;
			if (entry.expanded)
				semantics.states |= AccessibilityState.Expanded;
			semantics.positionInSet = entry.depth + 1;
			semantics.hierarchyLevel = entry.depth + 1;
			node.semantics = semantics;
			node.on(UiEventKind.Click, function(_) { onSelect(); });
			node.on(UiEventKind.Activate, function(_) { onSelect(); onActivate(); });
			node.on(UiEventKind.KeyDown, onKey);
			node.on(UiEventKind.KeyRepeat, onKey);

			var disclosure:View = entry.hasChildren
				? new TreeDisclosure("disclosure-control", entry.expanded, onToggle)
				: new Spacer("disclosure-spacer", LayoutAxis.fixed(16.0), LayoutAxis.grow());
			node.add(context.withScope(new Key("disclosure"), function() return disclosure.build(context)));
			node.add(context.withScope(new Key("content"), function() return child.build(context)));
			onBuilt(node.id);
			return node;
		});
	}
}

private class TreeDisclosure implements View {
	final key:String;
	final expanded:Bool;
	final onToggle:Void->Void;

	public function new(key:String, expanded:Bool, onToggle:Void->Void) {
		this.key = key;
		this.expanded = expanded;
		this.onToggle = onToggle;
	}

	public function build(context:BuildContext):RenderNode {
		return context.withScope(new Key(key), function() {
			var style = new LayoutStyle();
			style.width = LayoutAxis.fixed(16.0);
			style.height = LayoutAxis.grow();
			var node = new Text(expanded ? "▾" : "▸", style).build(context);
			node.semantics = null;
			node.on(UiEventKind.Click, function(_) { onToggle(); });
			node.on(UiEventKind.Activate, function(_) { onToggle(); });
			return node;
		});
	}
}
