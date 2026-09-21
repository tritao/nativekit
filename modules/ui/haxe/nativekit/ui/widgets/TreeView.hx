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

/** Model-backed virtual tree with lazy visible-branch indexing and stable selection state. */
class TreeView implements View {
	public final key:String;
	public final model:TreeViewModel;
	public final viewportStyle:LayoutStyle;
	public final virtualization:VirtualizationPolicy;
	public var controller(default, null):ScrollController;
	public var materializedFirst(default, null):Int;
	public var materializedLast(default, null):Int;
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
	var rootKeys:Array<String>;
	var rootOffsets:Array<Int>;
	var visibleCount:Int;
	var cachedEstimatedExtent:Float;
	var expandedBranches:Map<String, TreeBranch>;
	var indexByKey:Map<String, Int>;
	var entryByKey:Map<String, TreeEntry>;
	var cachedUniform:Bool;
	var extentIndex:Null<VirtualExtentIndex>;
	var itemIds:Map<String, WidgetId>;

	public function new(key:String, model:TreeViewModel, ?viewportStyle:LayoutStyle,
			?controller:ScrollController, viewportHeight:Float = 300.0,
			?selectedKey:String, ?expandedKeys:Array<String>,
			?onSelectionChanged:String->Void, ?onItemActivated:String->Void,
			?onExpandedChanged:String->Bool->Void, ?virtualization:VirtualizationPolicy) {
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
		this.virtualization = virtualization == null ? new VirtualizationPolicy() : virtualization;
		materializedFirst = 0;
		materializedLast = 0;
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
		rootKeys = [];
		rootOffsets = [0];
		visibleCount = 0;
		cachedEstimatedExtent = 0.0;
		expandedBranches = new Map();
		indexByKey = new Map();
		entryByKey = new Map();
		cachedUniform = false;
		extentIndex = null;
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
		if (entry == null) {
			var index = indexByKey.get(nodeKey);
			if (index == null)
				throw "TreeView expansion key is not visible";
			entry = entryAt(index);
		}
		ensureEntryDetails(entry);
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
		if (entry == null) {
			var index = indexByKey.get(nodeKey);
			if (index == null)
				return false;
			entry = entryAt(index);
		}
		return entry.expanded;
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
			var window = requiredExtentIndex();
			window.update(viewportHeight, controller.offsetY, virtualization.leadingOverscan,
				virtualization.trailingOverscan);
			measureWindow(window.first, window.last);
			window.update(viewportHeight, controller.offsetY, virtualization.leadingOverscan,
				virtualization.trailingOverscan);
			measureWindow(window.first, window.last);
			window.update(viewportHeight, controller.offsetY, virtualization.leadingOverscan,
				virtualization.trailingOverscan);
			materializedFirst = window.first;
			materializedLast = window.last;
			var rowViews:Array<KeyedView> = [];
			if (window.count > 0) {
				rowViews.push(new KeyedView("before", new Spacer("before-spacer",
					LayoutAxis.grow(), LayoutAxis.fixed(window.startOffset(window.first)))));
				for (index in window.first...window.last) {
					var entry = entryAt(index);
					ensureEntryDetails(entry);
					entryByKey.set(entry.key, entry);
					var nodeKey = entry.key;
					var item = model.buildItem(nodeKey);
					if (item == null)
						throw 'TreeView model returned null for key $nodeKey';
					var row = new TreeViewRow("row", nodeKey, item, entry, selectedKey == nodeKey,
						function() { select(nodeKey); },
						function() { if (onItemActivated != null) onItemActivated(nodeKey); },
						function() { toggleExpanded(nodeKey); },
						function(event) { handleNodeKey(context, entry, event); },
						function(id) { itemIds.set(nodeKey, id); });
					var slotKey = virtualization.recycleSlots ? 'slot:${index - window.first}' :
						'item:$nodeKey';
					rowViews.push(new KeyedView(slotKey, row));
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
			semantics.setSize = visibleCount;
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
			case UiKey.Home: nextKey = visibleCount == 0 ? null : entryAt(0).key;
			case UiKey.End: nextKey = visibleCount == 0 ? null : entryAt(visibleCount - 1).key;
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
		if (current == null || visibleCount == 0)
			return null;
		var next = Std.int(Math.max(0, Math.min(visibleCount - 1, current + delta)));
		return entryAt(next).key;
	}

	function firstChildKey(parentKey:String):Null<String> {
		var parent = indexByKey.get(parentKey);
		if (parent == null || parent + 1 >= visibleCount)
			return null;
		var child = entryAt(parent + 1);
		return child.parentKey == parentKey ? child.key : null;
	}

	function visibleNodeCount():Int {
		if (extentIndex == null)
			return 1;
		var window = requiredExtentIndex();
		if (window.count == 0)
			return 1;
		return Std.int(Math.max(1.0, Math.ceil(window.viewportExtent /
			Math.max(1.0, window.extentAt(window.first)))));
	}

	function ensureTreeMetrics():Void {
		var modelRevision = model.revision();
		if (cachedModelRevision == modelRevision && cachedExpansionRevision == expansionRevision &&
			extentIndex != null)
			return;
		var estimatedExtent = model.estimatedExtent();
		if (estimatedExtent <= 0.0 || !finite(estimatedExtent))
			throw "TreeView estimated extent must be finite and positive";
		cachedUniform = model.extentIsUniform();
		var rootCount = model.rootCount();
		if (rootCount < 0)
			throw "TreeView root count must be non-negative";
		var roots:Array<String> = [];
		var visibleKeys:Map<String, Bool> = new Map();
		for (index in 0...rootCount) {
			var rootKey = model.rootKeyAt(index);
			if (rootKey == null || rootKey.length == 0)
				throw 'TreeView root $index has an empty key';
			if (visibleKeys.exists(rootKey))
				throw 'TreeView contains a duplicate visible key $rootKey';
			visibleKeys.set(rootKey, true);
			roots.push(rootKey);
		}

		cachedEstimatedExtent = estimatedExtent;
		rootKeys = roots;
		rootOffsets = [0];
		expandedBranches = new Map();
		visibleCount = 0;
		for (rootKey in roots) {
			var branch = buildBranch(rootKey, null, 0, visibleKeys);
			if (branch == null)
				visibleCount++;
			else {
				expandedBranches.set(rootKey, branch);
				visibleCount += branch.visibleCount;
			}
			rootOffsets.push(visibleCount);
		}

		indexByKey = new Map();
		entryByKey = new Map();
		for (index in 0...roots.length)
			indexByKey.set(roots[index], rootOffsets[index]);
		for (index in 0...roots.length) {
			var rootKey = roots[index];
			var rootBranch = expandedBranches.get(rootKey);
			if (rootBranch != null)
				registerBranch(rootBranch, rootOffsets[index] + 1);
		}
		cachedModelRevision = modelRevision;
		cachedExpansionRevision = expansionRevision;
		extentIndex = new VirtualExtentIndex(visibleCount, estimatedExtent,
			fallbackViewportHeight, controller.offsetY,
			virtualization.leadingOverscan, virtualization.trailingOverscan);
		if (selectedKey != null && !indexByKey.exists(selectedKey)) {
			selectedKey = null;
			updateSelectedState(null);
		}
	}

	function buildBranch(nodeKey:String, parentKey:Null<String>, depth:Int,
			visibleKeys:Map<String, Bool>):Null<TreeBranch> {
		var requestedExpanded = expansionFor(nodeKey);
		if (!requestedExpanded)
			return null;
		var childCount = model.childCount(nodeKey);
		if (childCount < 0)
			throw 'TreeView child count for $nodeKey must be non-negative';
		var branch = new TreeBranch(nodeKey, parentKey, depth, cachedEstimatedExtent);
		branch.detailsKnown = true;
		branch.hasChildren = childCount > 0;
		if (!branch.hasChildren) {
			branch.expanded = false;
			return branch;
		}
		branch.expanded = true;
		for (childIndex in 0...childCount) {
			var childKey = model.childKeyAt(nodeKey, childIndex);
			if (childKey == null || childKey.length == 0)
				throw 'TreeView child $nodeKey:$childIndex has an empty key';
			if (visibleKeys.exists(childKey))
				throw 'TreeView contains a duplicate visible key $childKey';
			visibleKeys.set(childKey, true);
			var nested = buildBranch(childKey, nodeKey, depth + 1, visibleKeys);
			branch.children.push(nested == null
				? new TreeBranch(childKey, nodeKey, depth + 1, cachedEstimatedExtent)
				: nested);
		}
		branch.visibleCount = 1;
		for (child in branch.children)
			branch.visibleCount += child.visibleCount;
		return branch;
	}

	function registerBranch(branch:TreeBranch, startIndex:Int):Void {
		var nextIndex = startIndex;
		for (child in branch.children) {
			if (indexByKey.exists(child.key) && indexByKey.get(child.key) != nextIndex)
				throw 'TreeView contains a duplicate visible key ${child.key}';
			indexByKey.set(child.key, nextIndex);
			if (child.expanded)
				registerBranch(child, nextIndex + 1);
			nextIndex += child.visibleCount;
		}
	}

	function entryAt(index:Int):TreeEntry {
		if (index < 0 || index >= visibleCount)
			throw "TreeView visible index is out of range";
		var low = 0;
		var high = rootKeys.length;
		while (low < high) {
			var middle = (low + high) >> 1;
			if (rootOffsets[middle + 1] <= index)
				low = middle + 1;
			else
				high = middle;
		}
		var rootIndex = low;
		var rootKey = rootKeys[rootIndex];
		var rootBranch = expandedBranches.get(rootKey);
		var localIndex = index - rootOffsets[rootIndex];
		var result:TreeEntry;
		if (rootBranch == null)
			result = entryByKey.get(rootKey);
		else
			result = branchEntryAt(rootBranch, localIndex);
		if (result == null)
			result = new TreeEntry(rootKey, null, 0, false, false, cachedEstimatedExtent, false);
		entryByKey.set(result.key, result);
		return result;
	}

	function branchEntryAt(branch:TreeBranch, index:Int):TreeEntry {
		if (index == 0)
			return branch;
		var remaining = index - 1;
		for (child in branch.children) {
			if (remaining < child.visibleCount)
				return branchEntryAt(child, remaining);
			remaining -= child.visibleCount;
		}
		throw "TreeView branch index is out of range";
	}

	function ensureEntryDetails(entry:TreeEntry):Void {
		if (entry.detailsKnown)
			return;
		var childCount = model.childCount(entry.key);
		if (childCount < 0)
			throw 'TreeView child count for ${entry.key} must be non-negative';
		entry.hasChildren = childCount > 0;
		entry.detailsKnown = true;
	}

	function measureWindow(first:Int, last:Int):Void {
		if (extentIndex == null || cachedUniform)
			return;
		var indexMetrics = requiredExtentIndex();
		for (index in first...last) {
			var entry = entryAt(index);
			ensureEntryDetails(entry);
			var extent = model.extentAt(entry.key);
			if (extent <= 0.0 || !finite(extent))
				throw 'TreeView extent for ${entry.key} must be finite and positive';
			entry.extent = extent;
			indexMetrics.setExtent(index, extent);
		}
	}

	function expansionFor(nodeKey:String):Bool {
		if (expandedKeys.exists(nodeKey))
			return expandedKeys.get(nodeKey);
		var expanded = model.initiallyExpanded(nodeKey);
		if (expanded)
			expandedKeys.set(nodeKey, true);
		return expanded;
	}

	function updateSelectedState(nextKey:Null<String>):Void {
		if (selectedState == null)
			return;
		var state:State<String> = cast selectedState;
		state.update(nextKey == null ? "" : nextKey);
	}

	function extentOffset(index:Int):Float {
		if (extentIndex == null)
			return 0.0;
		var window = requiredExtentIndex();
		return window.startOffset(index);
	}

	function requiredExtentIndex():VirtualExtentIndex {
		var result = extentIndex;
		if (result == null)
			throw "TreeView extent metrics are not initialized";
		return result;
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
	public var hasChildren:Bool;
	public var expanded:Bool;
	public var extent:Float;
	public var detailsKnown:Bool;

	public function new(key:String, parentKey:Null<String>, depth:Int,
			hasChildren:Bool, expanded:Bool, extent:Float, detailsKnown:Bool = true) {
		this.key = key;
		this.parentKey = parentKey;
		this.depth = depth;
		this.hasChildren = hasChildren;
		this.expanded = expanded;
		this.extent = extent;
		this.detailsKnown = detailsKnown;
	}
}

private class TreeBranch extends TreeEntry {
	public final children:Array<TreeBranch>;
	public var visibleCount:Int;

	public function new(key:String, parentKey:Null<String>, depth:Int, extent:Float) {
		super(key, parentKey, depth, false, false, extent, false);
		children = [];
		visibleCount = 1;
	}
}

private class TreeViewRow implements View {
	final key:String;
	final itemKey:String;
	final child:View;
	final entry:TreeEntry;
	final selected:Bool;
	final onSelect:Void->Void;
	final onActivate:Void->Void;
	final onToggle:Void->Void;
	final onKey:UiEvent->Void;
	final onBuilt:WidgetId->Void;

	public function new(key:String, itemKey:String, child:View, entry:TreeEntry, selected:Bool,
			onSelect:Void->Void, onActivate:Void->Void, onToggle:Void->Void,
			onKey:UiEvent->Void, onBuilt:WidgetId->Void) {
		this.key = key;
		this.itemKey = itemKey;
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
			node.add(new KeyedView('item:$itemKey', child).build(context));
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
