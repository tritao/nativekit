import FontCollection;
import LayoutAxis;
import LayoutDirection;
import LayoutFrame;
import LayoutSession;
import LayoutStyle;
import LayoutVisualKind;
import ResolvedLayoutItem;
import nativekit.ui.core.Key;
import nativekit.ui.core.RenderNode;
import nativekit.ui.core.UiContext;
import nativekit.ui.core.UiEventKind;
import nativekit.ui.core.UiKey;
import nativekit.ui.core.UiModifier;
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
import nativekit.ui.widgets.ComboBox;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.Dialog;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Menu;
import nativekit.ui.widgets.MenuItem;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Select;
import nativekit.ui.widgets.SelectOption;
import nativekit.ui.widgets.Stack;
import nativekit.ui.widgets.StackChild;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.widgets.Tabs;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.Toggle;
import nativekit.ui.widgets.Utf8Text;
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

		var selectChanges = 0;
		var selectedSelectValue = "";
		var select = new Select("accessibility-select", [
			new SelectOption("one", "Öne", "one"),
			new SelectOption("blocked", "Blocked", "blocked", false),
			new SelectOption("two", "Two", "two")
		], "blocked", function(next) {
			selectChanges++;
			selectedSelectValue = next;
		});
		var selectRoot = context.submit(select, frame);
		var selectTrigger = selectRoot.children[0];
		var selectSemantics:Semantics = cast selectTrigger.semantics;
		if (selectRoot.children.length != 1 || selectSemantics.role != AccessibilityRole.ComboBox ||
			selectSemantics.value != "Öne" ||
			selectSemantics.documentLength != Utf8Text.length("Öne") ||
			(selectSemantics.states & AccessibilityState.HasPopup) == 0 ||
			(selectSemantics.actions & AccessibilityAction.SetValue) == 0 ||
			(selectSemantics.actions & AccessibilityAction.Expand) == 0 ||
			!context.focusWidget(selectTrigger.id))
			return 19;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		selectRoot = context.submit(select, frame);
		var openSelectSemantics:Semantics = cast selectRoot.children[0].semantics;
		var optionListSemantics:Semantics = cast selectRoot.children[1].semantics;
		var selectedOptionSemantics:Semantics = cast selectRoot.children[1].children[0].semantics;
		if (selectRoot.children.length != 2 || !selectRoot.focusTrap ||
			select.value != "one" || (openSelectSemantics.states & AccessibilityState.Expanded) == 0 ||
			(openSelectSemantics.actions & AccessibilityAction.Collapse) == 0 ||
			optionListSemantics.role != AccessibilityRole.List ||
			optionListSemantics.orientation != AccessibilityOrientation.Vertical ||
			selectedOptionSemantics.setSize != 3 || selectedOptionSemantics.positionInSet != 1 ||
			(selectedOptionSemantics.states & AccessibilityState.Selected) == 0 ||
			selectRoot.children[1].children[1].enabled ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(selectRoot.children[0].id))
			return 20;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		if (context.focus.focusedId == null ||
			!context.focus.focusedId.equals(selectRoot.children[1].children[2].id) ||
			select.value != "one")
			return 21;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (select.value != "two" || selectedSelectValue != "two" || selectChanges != 1 ||
			context.focus.focusedId == null || !context.focus.focusedId.equals(selectRoot.children[0].id))
			return 22;
		selectRoot = context.submit(select, frame);
		if (selectRoot.children.length != 1 ||
			(cast(selectRoot.children[0].semantics, Semantics).value != "Two") ||
			!AccessibilityAudit.isValid(selectRoot))
			return 23;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		selectRoot = context.submit(select, frame);
		if (selectRoot.children.length != 2)
			return 24;
		var optionGeometry:ResolvedLayoutItem = cast selectRoot.children[1].children[0].resolved;
		var optionX = optionGeometry.x + optionGeometry.width * 0.5;
		var optionY = optionGeometry.y + optionGeometry.height * 0.5;
		context.pointerDown(optionX, optionY, 0);
		context.pointerUp(optionX, optionY, 0);
		selectRoot = context.submit(select, frame);
		if (select.value != "one" || selectedSelectValue != "one" || selectChanges != 2 ||
			selectRoot.children.length != 1 || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(selectRoot.children[0].id))
			return 25;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		selectRoot = context.submit(select, frame);
		if (selectRoot.children.length != 2)
			return 27;
		context.key(UiEventKind.KeyDown, UiKey.Escape);
		selectRoot = context.submit(select, frame);
		if (selectRoot.children.length != 1 || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(selectRoot.children[0].id))
			return 28;
		if (!context.accessibilityAction(selectRoot.children[0].id.value, AccessibilityRequest.SetValue,
			"two", -1, -1, 1) || select.value != "two" || selectedSelectValue != "two" ||
			selectChanges != 3)
			return 29;
		if (!checkSelectOverlay(context))
			return 30;
		if (!checkSelectOverlap(context))
			return 46;
		if (!checkMovingHover(context))
			return 47;
		if (!checkSelectScroll(context))
			return 31;
		if (!checkComboBox(context))
			return 32;

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
			"modal-without-focus-trap", "text-range-out-of-bounds", "selection-out-of-bounds"])
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

	static function checkSelectOverlay(context:UiContext):Bool {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(96.0);
		style.height = LayoutAxis.fixed(36.0);
		var positioned = new Select("positioned-select", [
			new SelectOption("one", "One", "one"),
			new SelectOption("two", "Two", "two"),
			new SelectOption("three", "Three", "three")
		], "one", null, style);
		var stack = new Stack("positioned-select-stack", [
			new StackChild("select", positioned, 220.0, 150.0, 1,
				LayoutAxis.fixed(96.0), LayoutAxis.fixed(36.0), false)
		]);
		var frame = new LayoutFrame(256.0, 192.0);
		var root = context.submit(stack, frame);
		var selectRoot = root.children[0];
		var trigger = selectRoot.children[0];
		if (!context.focusWidget(trigger.id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		root = context.submit(stack, frame);
		selectRoot = root.children[0];
		if (selectRoot.children.length != 2)
			return false;
		var triggerGeometry:ResolvedLayoutItem = cast selectRoot.children[0].resolved;
		var dropdownGeometry:ResolvedLayoutItem = cast selectRoot.children[1].resolved;
		if (dropdownGeometry.x < 8.0 ||
			dropdownGeometry.x + dropdownGeometry.width > 248.0 ||
			dropdownGeometry.y < 0.0 ||
			dropdownGeometry.y + dropdownGeometry.height > 192.0 ||
			dropdownGeometry.y + dropdownGeometry.height > triggerGeometry.y - 3.5)
			return false;
		context.pointerDown(4.0, 4.0, 0);
		context.pointerUp(4.0, 4.0, 0);
		root = context.submit(stack, frame);
		return root.children[0].children.length == 1 && context.focus.focusedId != null &&
			context.focus.focusedId.equals(root.children[0].children[0].id);
	}

	static function checkSelectOverlap(context:UiContext):Bool {
		var selected = "compact";
		var underlyingClicks = 0;
		var select = new Select("overlap-select", [
			new SelectOption("comfortable", "Comfortable", "comfortable"),
			new SelectOption("compact", "Compact", "compact")
		], selected, function(value) { selected = value; });
		var column = new Column("overlap-column", [
			new KeyedView("select", select),
			new KeyedView("underlying",
				new Button("Underlying", null, function() { underlyingClicks++; }))
		]);
		var frame = new LayoutFrame(256.0, 192.0);
		var root = context.submit(column, frame);
		var trigger = root.children[0].children[0];
		if (!context.focusWidget(trigger.id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		root = context.submit(column, frame);
		var selectRoot = root.children[0];
		if (selectRoot.children.length != 2 || selectRoot.layout.style.zIndex <= 0)
			return false;
		var option:ResolvedLayoutItem = cast selectRoot.children[1].children[0].resolved;
		var x = option.x + 3.0;
		var y = option.y + option.height * 0.5;
		context.pointerDown(x, y, 0);
		context.pointerUp(x, y, 0);
		return selected == "comfortable" && underlyingClicks == 0;
	}

	static function checkMovingHover(context:UiContext):Bool {
		var frame = new LayoutFrame(256.0, 192.0);
		var initial = new Stack("moving-hover-stack", [
			new StackChild("target", new Button("Moving target"), 8.0, 8.0, 1,
				LayoutAxis.fixed(100.0), LayoutAxis.fixed(36.0))
		]);
		var root = context.submit(initial, frame);
		var targetId = root.children[0].id;
		context.pointerMove(12.0, 12.0);
		if (context.events.hoveredId() == null ||
			!context.events.hoveredId().equals(targetId))
			return false;
		var moved = new Stack("moving-hover-stack", [
			new StackChild("target", new Button("Moving target"), 140.0, 8.0, 1,
				LayoutAxis.fixed(100.0), LayoutAxis.fixed(36.0))
		]);
		context.submit(moved, frame);
		return context.events.hoveredId() == null ||
			!context.events.hoveredId().equals(targetId);
	}

	static function checkSelectScroll(context:UiContext):Bool {
		var options:Array<SelectOption<String>> = [];
		for (index in 0...12)
			options.push(new SelectOption("item-" + index, "Item " + index, "item-" + index));
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(120.0);
		style.height = LayoutAxis.fixed(36.0);
		var select = new Select("scrolling-select", options, "item-0", null, style);
		var stack = new Stack("scrolling-select-stack", [
			new StackChild("select", select, 8.0, 8.0, 1,
				LayoutAxis.fixed(120.0), LayoutAxis.fixed(36.0), false)
		]);
		var frame = new LayoutFrame(256.0, 192.0);
		var root = context.submit(stack, frame);
		var selectRoot = root.children[0];
		var trigger = selectRoot.children[0];
		if (!context.focusWidget(trigger.id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		root = context.submit(stack, frame);
		selectRoot = root.children[0];
		if (selectRoot.children.length != 2 || selectRoot.children[1].children.length != 1)
			return false;
		var dropdown = selectRoot.children[1];
		var listSemantics:Semantics = cast dropdown.semantics;
		var viewport = dropdown.children[0];
		var content = viewport.children[0];
		var viewportGeometry:ResolvedLayoutItem = cast viewport.resolved;
		var contentGeometry:ResolvedLayoutItem = cast content.resolved;
		if ((listSemantics.actions & AccessibilityAction.ScrollForward) == 0 ||
			viewportGeometry.height >= 144.0 || contentGeometry.transform.ty != 0.0)
			return false;
		var beforeWheel = contentGeometry.transform.ty;
		context.scroll(viewportGeometry.x + 3.0, viewportGeometry.y + 3.0, 0.0, 64.0);
		root = context.submit(stack, frame);
		content = root.children[0].children[1].children[0].children[0];
		var afterWheel:ResolvedLayoutItem = cast content.resolved;
		if (afterWheel.transform.ty >= beforeWheel - 0.1)
			return false;
		var dropdownId = root.children[0].children[1].id;
		if (!context.accessibilityAction(dropdownId.value, AccessibilityRequest.ScrollForward,
			null, -1, -1, 1))
			return false;
		root = context.submit(stack, frame);
		content = root.children[0].children[1].children[0].children[0];
		var afterAccessibility:ResolvedLayoutItem = cast content.resolved;
		if (afterAccessibility.transform.ty >= afterWheel.transform.ty - 0.1)
			return false;
		if (!context.accessibilityAction(dropdownId.value, AccessibilityRequest.ScrollBackward,
			null, -1, -1, 1))
			return false;
		root = context.submit(stack, frame);
		content = root.children[0].children[1].children[0].children[0];
		var beforeEnd:ResolvedLayoutItem = cast content.resolved;
		context.key(UiEventKind.KeyDown, UiKey.End);
		root = context.submit(stack, frame);
		selectRoot = root.children[0];
		viewport = selectRoot.children[1].children[0];
		content = viewport.children[0];
		if (content.children.length < options.length)
			return false;
		var lastOption = content.children[options.length - 1];
		var endContent:ResolvedLayoutItem = cast content.resolved;
		var focusedId = context.focus.focusedId;
		if (focusedId == null || !focusedId.equals(lastOption.id))
			return false;
		return endContent.transform.ty < beforeEnd.transform.ty - 0.1;
	}

	static function checkComboBox(context:UiContext):Bool {
		var changes = 0;
		var selected = "";
		var combo = new ComboBox("accessibility-combo", [
			new SelectOption("one", "One", "one"),
			new SelectOption("two", "Two", "two"),
			new SelectOption("blocked", "Blocked", "blocked", false)
		], "one", function(value) {
			changes++;
			selected = value;
		});
		var frame = new LayoutFrame(256.0, 192.0);
		var root = context.submit(combo, frame);
		var input = root.children[0];
		var semantics:Semantics = cast input.semantics;
		if (root.children.length != 1 || semantics.role != AccessibilityRole.ComboBox ||
			semantics.value != "One" || (semantics.states & AccessibilityState.HasPopup) == 0 ||
			(semantics.actions & AccessibilityAction.SetValue) == 0 ||
			(semantics.actions & AccessibilityAction.Expand) == 0 ||
			!context.focusWidget(input.id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Space);
		root = context.submit(combo, frame);
		if (root.children.length != 2)
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Escape);
		root = context.submit(combo, frame);
		if (root.children.length != 1)
			return false;
		input = root.children[0];
		context.key(UiEventKind.KeyDown, UiKey.A, UiModifier.Control);
		context.text(UiEventKind.TextInput, "Tw");
		root = context.submit(combo, frame);
		if (root.children.length != 2 || root.children[1].children.length != 1 ||
			(cast(root.children[1].children[0].semantics, Semantics)).label != "Two")
			return false;
		input = root.children[0];
		context.key(UiEventKind.KeyDown, UiKey.A, UiModifier.Control);
		context.text(UiEventKind.TextInput, "Zed");
		root = context.submit(combo, frame);
		if (root.children.length != 1)
			return false;
		if (!context.accessibilityAction(root.children[0].id.value, AccessibilityRequest.SetValue,
			"one", -1, -1, 1))
			return false;
		root = context.submit(combo, frame);
		if (root.children.length != 1)
			return false;
		input = root.children[0];
		context.key(UiEventKind.KeyDown, UiKey.Down);
		root = context.submit(combo, frame);
		input = root.children[0];
		semantics = cast input.semantics;
		var listSemantics:Semantics = cast root.children[1].semantics;
		if (root.children.length != 2 || !root.focusTrap ||
			(semantics.states & AccessibilityState.Expanded) == 0 ||
			(semantics.actions & AccessibilityAction.Collapse) == 0 ||
			listSemantics.role != AccessibilityRole.List ||
			root.children[1].children.length != 3)
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Down);
		if (context.focus.focusedId == null ||
			!context.focus.focusedId.equals(root.children[1].children[1].id))
			return false;
		context.key(UiEventKind.KeyDown, UiKey.Enter);
		if (combo.value != "two" || selected != "two" || changes != 1)
			return false;
		root = context.submit(combo, frame);
		input = root.children[0];
		if (root.children.length != 1 || context.focus.focusedId == null ||
			!context.focus.focusedId.equals(input.id) || !AccessibilityAudit.isValid(root))
			return false;
		if (!context.accessibilityAction(input.id.value, AccessibilityRequest.SetValue,
			"one", -1, -1, 1) || combo.value != "one" || selected != "one" || changes != 2)
			return false;
		return true;
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
		var invalidText = new RenderNode(new WidgetId(900014));
		var invalidTextSemantics = new Semantics(AccessibilityRole.TextField, "Invalid text", "value");
		invalidTextSemantics.documentLength = 0;
		invalidTextSemantics.selectionStart = 3;
		invalidTextSemantics.selectionEnd = 2;
		invalidText.semantics = invalidTextSemantics;
		root.add(invalidText);
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
