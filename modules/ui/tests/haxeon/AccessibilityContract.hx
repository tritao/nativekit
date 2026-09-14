import FontCollection;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutSession;
import LayoutStyle;
import LayoutVisualKind;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.WidgetId;
import nativekit.ui.debug.AccessibilityAudit;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityBridge;
import nativekit.ui.semantics.AccessibilityOrientation;
import nativekit.ui.semantics.AccessibilityRequest;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilitySnapshotNode;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.widgets.Tabs;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.VirtualList;

/** Headless deterministic contract checks for the Haxe accessibility layer. */
class AccessibilityContract {
	public static function run(fonts:FontCollection):Int {
		var context = new UiContext(LayoutSession.create(), fonts);
		var frame = new LayoutFrame(256.0, 192.0);
		var changes = 0;
		var tabs = new Tabs("accessibility-tabs", [
			new TabItem("first", "First", new Text("First content")),
			new TabItem("second", "Second", new Text("Second content"))
		], "first", function(_) { changes++; });
		var tabsRoot = context.submit(tabs, frame);
		var tabsSnapshot = AccessibilityBridge.project(tabsRoot, tabsRoot.children[0].children[1].id);
		var tabList = findSnapshot(tabsSnapshot, AccessibilityRole.TabList);
		var firstTab = findSnapshot(tabsSnapshot, AccessibilityRole.Tab);
		var tabPanel = findSnapshot(tabsSnapshot, AccessibilityRole.TabPanel);
		var tabListRecord:AccessibilitySnapshotNode = cast tabList;
		var firstTabRecord:AccessibilitySnapshotNode = cast firstTab;
		var tabOrientation:Int = cast tabListRecord.orientation;
		var expectedTabOrientation:Int = cast AccessibilityOrientation.Horizontal;
		var tabHasSelect = (firstTabRecord.actions & AccessibilityAction.Select) != 0;
		var firstTabSelected = (firstTabRecord.states & AccessibilityState.Selected) != 0;
		if (tabList == null || firstTab == null || tabPanel == null ||
			tabOrientation != expectedTabOrientation || !tabHasSelect || !firstTabSelected)
			return 1;
		var secondTabNode = tabsRoot.children[0].children[1];
		if (!context.accessibilityAction(secondTabNode.id.value, AccessibilityRequest.Select,
			null, -1, -1, 1) || tabs.selectedKey != "second" || changes != 1)
			return 2;
		tabsRoot = context.submit(tabs, frame);
		var selectedTab:Null<Semantics> = cast tabsRoot.children[0].children[1].semantics;
		if (selectedTab == null || (selectedTab.states & AccessibilityState.Selected) == 0)
			return 3;

		var parents:Map<Int, Int> = new Map();
		for (item in tabsSnapshot)
			parents.set(item.id, item.parentId);
		var atomic = AccessibilityBridge.buildUpdate(tabsSnapshot, new Map(), secondTabNode.id.value);
		if (atomic.nativeUpdate.get_struct_size() != NativeKit.AccessibilityUpdate.size() ||
			atomic.nativeUpdate.get_node_count() != tabsSnapshot.length || atomic.removedNodeCount != 0 ||
			atomic.nativeUpdate.get_focus() != secondTabNode.id.value ||
			(atomic.nativeUpdate.get_flags() &
			NativeKit.AccessibilityUpdateFlags.NkAccessibilityUpdateFocus) == 0)
			return 4;
		var allRemoved = AccessibilityBridge.buildUpdate([], parents,
			NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT);
		var removedId = tabsSnapshot[0].id;
		if (allRemoved.nativeUpdate.get_node_count() != 0 || allRemoved.removedNodeCount != 1 ||
			allRemoved.removedNodeIds.length != 4 ||
			allRemoved.removedNodeIds.get(0) != (removedId & 0xff) ||
			allRemoved.removedNodeIds.get(1) != ((removedId >>> 8) & 0xff) ||
			allRemoved.removedNodeIds.get(2) != ((removedId >>> 16) & 0xff) ||
			allRemoved.removedNodeIds.get(3) != ((removedId >>> 24) & 0xff) ||
			allRemoved.nativeUpdate.get_focus() != NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT)
			return 5;
		var nativeNodes = AccessibilityBridge.buildNodes(tabsSnapshot);
		var firstNativeRole:Int = cast nativeNodes[0].get_role();
		if (nativeNodes.length != tabsSnapshot.length ||
			nativeNodes[0].get_struct_size() != NativeKit.AccessibilityNode.size() ||
			firstNativeRole != AccessibilityRole.TabList)
			return 6;

		var dismissals = 0;
		var dialog = new Dialog("accessibility-dialog", "Preferences", new Button("Apply"),
			function() { dismissals++; });
		var dialogRoot = context.submit(dialog, frame);
		var dialogSemantics:Semantics = cast dialogRoot.semantics;
		var dialogSnapshot = AccessibilityBridge.project(dialogRoot, context.focus.focusedId);
		if (dialogSemantics.role != AccessibilityRole.Dialog ||
			(dialogSemantics.states & AccessibilityState.Modal) == 0 || !dialogRoot.focusTrap ||
			dialogSnapshot.length == 0 || dialogSnapshot[0].role != AccessibilityRole.Dialog ||
			!context.accessibilityAction(dialogRoot.id.value, AccessibilityRequest.Dismiss,
				null, -1, -1, 1) || dismissals != 1)
			return 7;

		var selectedMenu = "";
		var menuDismissals = 0;
		var menu = new Menu("accessibility-menu", [
			new MenuItem("save", "Save", function() { selectedMenu = "save"; })
		], 8.0, 8.0, function() { menuDismissals++; });
		var menuRoot = context.submit(menu, frame);
		var menuSnapshot = AccessibilityBridge.project(menuRoot, context.focus.focusedId);
		var menuItem = findSnapshot(menuSnapshot, AccessibilityRole.MenuItem);
		var menuRootSemantics:Semantics = cast menuRoot.semantics;
		var menuItemId = 0;
		menuRoot.walk(function(node) {
			var semantics:Null<Semantics> = cast node.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.MenuItem)
				menuItemId = node.id.value;
		});
		if (menuRootSemantics.role != AccessibilityRole.Menu ||
			(menuRootSemantics.states & AccessibilityState.Modal) == 0 || !menuRoot.focusTrap ||
			menuItem == null || (menuItem.actions & AccessibilityAction.Select) == 0 || menuItemId == 0 ||
			!context.accessibilityAction(menuItemId, AccessibilityRequest.Select, null, -1, -1, 1) ||
			selectedMenu != "save" || menuDismissals != 1)
			return 8;

		var toggles = 0;
		var toggle = new Toggle("accessibility-switch", "Enabled", false,
			function(value) { if (value) toggles++; });
		var toggleRoot = context.submit(toggle, frame);
		var toggleSemantics:Semantics = cast toggleRoot.semantics;
		if (toggleSemantics.role != AccessibilityRole.Switch ||
			(toggleSemantics.actions & AccessibilityAction.Toggle) == 0 ||
			!context.accessibilityAction(toggleRoot.id.value, AccessibilityRequest.Toggle,
				null, -1, -1, 1) || !toggle.checked || toggles != 1)
			return 9;

		var progressRoot = context.submit(new ProgressBar("accessibility-progress", 7.0,
			2.0, 10.0, "Transfer"), frame);
		var progressSemantics:Semantics = cast progressRoot.semantics;
		if (progressSemantics.role != AccessibilityRole.ProgressBar ||
			progressSemantics.numericValue != 7.0 || progressSemantics.numericMinimum != 2.0 ||
			progressSemantics.numericMaximum != 10.0 ||
			(progressSemantics.states & AccessibilityState.ReadOnly) == 0)
			return 10;

		var listStyle = new LayoutStyle();
		listStyle.width = LayoutAxis.fixed(256.0);
		listStyle.height = LayoutAxis.fixed(320.0);
		var controller = new nativekit.ui.widgets.ScrollController();
		var logicalRows = new VirtualList("accessibility-large-list", 10000, 20.0,
			function(index) return new Text('Row $index'), listStyle, null, controller, 320.0);
		context.submit(logicalRows, new LayoutFrame(256.0, 320.0));
		controller.jumpTo(0.0, 84600.0);
		var listRoot = context.submit(logicalRows, new LayoutFrame(256.0, 320.0));
		var listSnapshot = AccessibilityBridge.project(listRoot, null);
		var listSemantics:Semantics = cast listRoot.semantics;
		var logicalRow:Null<AccessibilitySnapshotNode> = null;
		for (item in listSnapshot)
			if (item.role == AccessibilityRole.CollectionItem && item.positionInSet == 4231)
				logicalRow = item;
		if (listSemantics.role != AccessibilityRole.Collection || listSemantics.setSize != 10000 ||
			listSemantics.orientation != AccessibilityOrientation.Vertical || logicalRow == null ||
			logicalRow.setSize != 10000 || logicalRow.positionInSet != 4231)
			return 11;

		var metadataRoot = context.submit(new AccessibilityMetadataView(), frame);
		var metadataSnapshot = AccessibilityBridge.project(metadataRoot,
			metadataRoot.children[1].children[0].id);
		var cell:Null<AccessibilitySnapshotNode> = findSnapshot(metadataSnapshot, AccessibilityRole.Cell);
		var gridRecord:Null<AccessibilitySnapshotNode> = findSnapshot(metadataSnapshot, AccessibilityRole.Grid);
		var treeItems:Array<AccessibilitySnapshotNode> = [];
		for (item in metadataSnapshot)
			if (item.role == AccessibilityRole.TreeItem)
				treeItems.push(item);
		if (cell == null || gridRecord == null || gridRecord.rowCount != 2 ||
			gridRecord.columnCount != 3 || gridRecord.orientation != AccessibilityOrientation.Horizontal ||
			cell.rowIndex != 1 ||
			cell.columnIndex != 2 || cell.rowSpan != 1 || cell.columnSpan != 1 ||
			cell.orientation != AccessibilityOrientation.Horizontal || treeItems.length != 2 ||
		treeItems[0].hierarchyLevel != 1 || treeItems[1].hierarchyLevel != 2 ||
			(treeItems[0].states & AccessibilityState.Expanded) == 0)
			return 12;
		var focusFound = false;
		for (item in metadataSnapshot)
			if (item.id == metadataRoot.children[1].children[0].id.value &&
				(item.states & AccessibilityState.Focused) != 0)
				focusFound = true;
		if (!focusFound || (AccessibilityBridge.project(metadataRoot, null)[0].states &
			AccessibilityState.Focused) != 0)
			return 13;
		var metadataNative = AccessibilityBridge.buildNodes(metadataSnapshot);
		var nativeCell:Null<NativeKit.AccessibilityNode> = null;
		var nativeGrid:Null<NativeKit.AccessibilityNode> = null;
		var nativeRow:Null<NativeKit.AccessibilityNode> = null;
		for (node in metadataNative) {
			var nativeRole:Int = cast node.get_role();
			if (nativeRole == AccessibilityRole.Cell)
				nativeCell = node;
			else if (nativeRole == AccessibilityRole.Grid)
				nativeGrid = node;
			else if (nativeRole == AccessibilityRole.Row)
				nativeRow = node;
		}
		var nativeGridOrientation:Int = nativeGrid == null ? -1 : cast nativeGrid.get_orientation();
		if (nativeCell == null || nativeGrid == null || nativeRow == null ||
			nativeGrid.get_row_count() != 2 || nativeGrid.get_column_count() != 3 ||
			nativeGridOrientation != AccessibilityOrientation.Horizontal ||
			nativeCell.get_row_index() != 1 || nativeCell.get_column_index() != 2 ||
			nativeCell.get_row_span() != 1 || nativeCell.get_column_span() != 1 ||
			nativeRow.get_hierarchy_level() != 1)
			return 14;

		var disabled = new Button("Disabled");
		disabled.enabled = false;
		var disabledRoot = context.submit(disabled, frame);
		var disabledSnapshot = AccessibilityBridge.project(disabledRoot, null);
		if (disabledSnapshot.length != 1 ||
			(disabledSnapshot[0].states & AccessibilityState.Disabled) == 0 ||
			(disabledSnapshot[0].states & AccessibilityState.Focusable) != 0 ||
			disabledSnapshot[0].actions != 0 ||
			AccessibilityBridge.buildUpdate(disabledSnapshot, new Map(), disabledSnapshot[0].id)
				.nativeUpdate.get_focus() != NativeKit.NativeKitConstants.NK_ACCESSIBILITY_ROOT)
			return 15;
		var disabledTree = context.submit(new DisabledSemanticContainer(), frame);
		var disabledTreeSnapshot = AccessibilityBridge.project(disabledTree,
			disabledTree.children[0].id);
		if (disabledTreeSnapshot.length != 2 ||
			(disabledTreeSnapshot[1].states & AccessibilityState.Disabled) == 0 ||
			(disabledTreeSnapshot[1].states & AccessibilityState.Focusable) != 0 ||
			disabledTreeSnapshot[1].actions != 0)
			return 18;

		var audit = makeAuditTree();
		var issues = AccessibilityAudit.inspect(audit);
		for (code in ["dialog-missing-name", "menu-item-missing-name", "tab-list-no-selected-tab",
			"tab-no-select", "switch-no-toggle", "expandable-missing-actions", "range-out-of-bounds",
			"collection-position-out-of-range", "grid-cell-out-of-bounds", "focusable-disabled",
			"modal-without-focus-trap"])
			if (!containsIssue(issues, code))
				return 16;

		var requestActions = [AccessibilityRequest.Toggle, AccessibilityRequest.Select,
			AccessibilityRequest.Deselect, AccessibilityRequest.Expand, AccessibilityRequest.Collapse,
			AccessibilityRequest.Dismiss, AccessibilityRequest.ShowContextMenu,
			AccessibilityRequest.ScrollIntoView];
		var requestCapabilities = [AccessibilityAction.Toggle, AccessibilityAction.Select,
			AccessibilityAction.Deselect, AccessibilityAction.Expand, AccessibilityAction.Collapse,
			AccessibilityAction.Dismiss, AccessibilityAction.ShowContextMenu,
			AccessibilityAction.ScrollIntoView];
		for (index in 0...requestActions.length) {
			var request = AccessibilityRequest.create(requestActions[index], null, -1, -1, 1);
			if (request == null || request.capability != requestCapabilities[index])
				return 17;
		}

		context.dispose();
		return 0;
	}

	static function findSnapshot(snapshot:Array<AccessibilitySnapshotNode>,
			role:AccessibilityRole):Null<AccessibilitySnapshotNode> {
		for (item in snapshot)
			if (item.role == role)
				return item;
		return null;
	}

	static function containsIssue(issues:Array<AccessibilityIssue>, code:String):Bool {
		for (issue in issues)
			if (issue.code == code)
				return true;
		return false;
	}

	static function makeAuditTree():RenderNode {
		var root = new RenderNode(new WidgetId(900000));
		var dialog = new RenderNode(new WidgetId(900001));
		var dialogSemantics = new Semantics(AccessibilityRole.Dialog, "");
		dialogSemantics.states |= AccessibilityState.Modal;
		dialog.semantics = dialogSemantics;
		root.add(dialog);
		var menuItem = new RenderNode(new WidgetId(900002));
		menuItem.semantics = new Semantics(AccessibilityRole.MenuItem, " ");
		root.add(menuItem);
		var tabList = new RenderNode(new WidgetId(900003));
		tabList.semantics = new Semantics(AccessibilityRole.TabList, "Options");
		var tab = new RenderNode(new WidgetId(900004));
		tab.semantics = new Semantics(AccessibilityRole.Tab, "First");
		tabList.add(tab);
		root.add(tabList);
		var toggle = new RenderNode(new WidgetId(900005));
		toggle.semantics = new Semantics(AccessibilityRole.Switch, "Sound");
		root.add(toggle);
		var treeItem = new RenderNode(new WidgetId(900006));
		treeItem.semantics = new Semantics(AccessibilityRole.TreeItem, "Folder");
		treeItem.add(new RenderNode(new WidgetId(900007)));
		root.add(treeItem);
		var sliderSemantics = new Semantics(AccessibilityRole.Slider, "Level");
		sliderSemantics.numericMinimum = 0.0;
		sliderSemantics.numericMaximum = 1.0;
		sliderSemantics.numericValue = 2.0;
		var slider = new RenderNode(new WidgetId(900008));
		slider.semantics = sliderSemantics;
		root.add(slider);
		var collectionItem = new RenderNode(new WidgetId(900009));
		var collectionSemantics = new Semantics(AccessibilityRole.CollectionItem);
		collectionSemantics.setSize = 16;
		collectionSemantics.positionInSet = 17;
		collectionItem.semantics = collectionSemantics;
		root.add(collectionItem);
		var grid = new RenderNode(new WidgetId(900010));
		var gridSemantics = new Semantics(AccessibilityRole.Grid);
		gridSemantics.rowCount = 2;
		gridSemantics.columnCount = 2;
		grid.semantics = gridSemantics;
		var cell = new RenderNode(new WidgetId(900011));
		var cellSemantics = new Semantics(AccessibilityRole.Cell);
		cellSemantics.rowIndex = 2;
		cellSemantics.columnIndex = 1;
		cell.semantics = cellSemantics;
		grid.add(cell);
		root.add(grid);
		var disabledParent = new RenderNode(new WidgetId(900012));
		disabledParent.enabled = false;
		var disabledFocus = new RenderNode(new WidgetId(900013));
		disabledFocus.focusable = true;
		disabledFocus.semantics = new Semantics(AccessibilityRole.Button, "Unavailable");
		disabledParent.add(disabledFocus);
		root.add(disabledParent);
		return root;
	}
}

private class DisabledSemanticContainer implements nativekit.ui.core.View {
	public function new() {}

	public function build(context:nativekit.ui.core.BuildContext):RenderNode {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.grow();
		var root = new RenderNode(context.id("disabled-container"), LayoutVisualKind.Box, style);
		root.enabled = false;
		root.semantics = new Semantics(AccessibilityRole.Group, "Unavailable controls");
		var child = new Button("Unavailable action");
		root.add(context.withScope(new Key("child"), function() return child.build(context)));
		return root;
	}
}

private class AccessibilityMetadataView implements nativekit.ui.core.View {
	public function new() {}

	public function build(context:nativekit.ui.core.BuildContext):RenderNode {
		var rootStyle = new LayoutStyle();
		rootStyle.width = LayoutAxis.fixed(256.0);
		rootStyle.height = LayoutAxis.fixed(192.0);
		rootStyle.direction = LayoutDirection.TopToBottom;
		var root = new RenderNode(context.id("metadata-root"), LayoutVisualKind.Box, rootStyle);
		root.semantics = new Semantics(AccessibilityRole.Group, "Metadata");

		var gridStyle = new LayoutStyle();
		gridStyle.width = LayoutAxis.grow();
		gridStyle.height = LayoutAxis.fixed(80.0);
		var grid = new RenderNode(context.id("grid"), LayoutVisualKind.Box, gridStyle);
		var gridSemantics = new Semantics(AccessibilityRole.Grid, "Grid");
		gridSemantics.rowCount = 2;
		gridSemantics.columnCount = 3;
		gridSemantics.orientation = AccessibilityOrientation.Horizontal;
		grid.semantics = gridSemantics;
		var rowStyle = new LayoutStyle();
		rowStyle.width = LayoutAxis.grow();
		rowStyle.height = LayoutAxis.fixed(32.0);
		var row = new RenderNode(context.id("row"), LayoutVisualKind.Box, rowStyle);
		var rowSemantics = new Semantics(AccessibilityRole.Row, "Second row");
		rowSemantics.hierarchyLevel = 1;
		row.semantics = rowSemantics;
		var cellStyle = new LayoutStyle();
		cellStyle.width = LayoutAxis.fixed(80.0);
		cellStyle.height = LayoutAxis.fixed(32.0);
		var cell = new RenderNode(context.id("cell"), LayoutVisualKind.Box, cellStyle);
		var cellSemantics = new Semantics(AccessibilityRole.Cell, "Cell");
		cellSemantics.rowIndex = 1;
		cellSemantics.columnIndex = 2;
		cellSemantics.rowSpan = 1;
		cellSemantics.columnSpan = 1;
		cellSemantics.orientation = AccessibilityOrientation.Horizontal;
		cell.semantics = cellSemantics;
		row.add(cell);
		grid.add(row);
		root.add(grid);

		var treeStyle = new LayoutStyle();
		treeStyle.width = LayoutAxis.grow();
		treeStyle.height = LayoutAxis.fixed(100.0);
		var tree = new RenderNode(context.id("tree"), LayoutVisualKind.Box, treeStyle);
		tree.semantics = new Semantics(AccessibilityRole.Tree, "Hierarchy");
		var parentStyle = new LayoutStyle();
		parentStyle.width = LayoutAxis.grow();
		parentStyle.height = LayoutAxis.fixed(40.0);
		var parent = new RenderNode(context.id("tree-item-parent"), LayoutVisualKind.Box, parentStyle);
		var parentSemantics = new Semantics(AccessibilityRole.TreeItem, "Parent");
		parentSemantics.hierarchyLevel = 1;
		parentSemantics.states |= AccessibilityState.Expanded;
		parentSemantics.actions = AccessibilityAction.Expand | AccessibilityAction.Collapse;
		parent.semantics = parentSemantics;
		var childStyle = new LayoutStyle();
		childStyle.width = LayoutAxis.grow();
		childStyle.height = LayoutAxis.fixed(32.0);
		var child = new RenderNode(context.id("tree-item-child"), LayoutVisualKind.Box, childStyle);
		var childSemantics = new Semantics(AccessibilityRole.TreeItem, "Child");
		childSemantics.hierarchyLevel = 2;
		child.semantics = childSemantics;
		parent.add(child);
		tree.add(parent);
		root.add(tree);
		return root;
	}
}
