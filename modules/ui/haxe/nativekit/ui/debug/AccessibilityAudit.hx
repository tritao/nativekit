package nativekit.ui.debug;

import nativekit.ui.core.RenderNode;
import nativekit.ui.semantics.AccessibilityAction;
import nativekit.ui.semantics.AccessibilityRole;
import nativekit.ui.semantics.AccessibilityState;
import nativekit.ui.semantics.Semantics;
import nativekit.ui.widgets.Utf8Text;

/** Small deterministic audit for common accessibility mistakes in resolved trees. */
class AccessibilityAudit {
	public static function inspect(root:Null<RenderNode>):Array<AccessibilityIssue> {
		var issues:Array<AccessibilityIssue> = [];
		if (root == null)
			return issues;
		var ids:Map<Int, Bool> = new Map();
		inspectNode(root, true, ids, issues);
		return issues;
	}

	public static function isValid(root:Null<RenderNode>):Bool
		return inspect(root).length == 0;

	static function inspectNode(node:RenderNode, ancestorsEnabled:Bool,
			ids:Map<Int, Bool>, issues:Array<AccessibilityIssue>):Void {
		if (ids.exists(node.id.value))
			issues.push(new AccessibilityIssue(node.id.value, "duplicate-id",
				"Render node IDs must be unique within a frame"));
		ids.set(node.id.value, true);
		var semantics:Null<Semantics> = cast node.semantics;
		if ((node.focusable || semantics != null && semantics.actions != 0) && semantics == null)
			issues.push(new AccessibilityIssue(node.id.value, "missing-semantics",
				"Interactive nodes need an accessibility role and state"));
		if (semantics != null) {
			var valueEnd = semantics.textStart + Utf8Text.length(semantics.value);
			if (semantics.textStart < 0 || semantics.documentLength < 0 ||
				valueEnd > semantics.documentLength)
				issues.push(new AccessibilityIssue(node.id.value, "text-range-out-of-bounds",
					'Text range ${semantics.textStart}...${valueEnd} must fit within document length ${semantics.documentLength}'));
			var noSelection = semantics.selectionStart == -1 && semantics.selectionEnd == -1;
			var validSelection = semantics.selectionStart >= semantics.textStart &&
				semantics.selectionStart <= semantics.selectionEnd &&
				semantics.selectionEnd <= valueEnd;
			if (!noSelection && !validSelection)
				issues.push(new AccessibilityIssue(node.id.value, "selection-out-of-bounds",
					'Selection ${semantics.selectionStart}...${semantics.selectionEnd} must be unset or fit within text range ${semantics.textStart}...${valueEnd}'));
			if ((semantics.role == AccessibilityRole.Dialog ||
				semantics.role == AccessibilityRole.MenuItem) &&
				(semantics.label == null || isBlank(semantics.label))) {
				var code = semantics.role == AccessibilityRole.Dialog
					? "dialog-missing-name" : "menu-item-missing-name";
				issues.push(new AccessibilityIssue(node.id.value, code,
					"This accessibility role needs a non-empty accessible name"));
			}
			if (needsName(semantics.role) &&
				(semantics.label == null || isBlank(semantics.label)))
				issues.push(new AccessibilityIssue(node.id.value, "missing-label",
					"Interactive accessibility nodes need a non-empty label"));
			if (semantics.role == AccessibilityRole.TabList && !hasSelectedTab(node))
				issues.push(new AccessibilityIssue(node.id.value, "tab-list-no-selected-tab",
					"A tab list needs one selected tab"));
			if (semantics.role == AccessibilityRole.Tab &&
				(semantics.actions & AccessibilityAction.Select) == 0)
				issues.push(new AccessibilityIssue(node.id.value, "tab-no-select",
					"Tabs need to expose the Select action"));
			if (semantics.role == AccessibilityRole.Switch &&
				(semantics.actions & AccessibilityAction.Toggle) == 0)
				issues.push(new AccessibilityIssue(node.id.value, "switch-no-toggle",
					"Switches need to expose the Toggle action"));
			var isExpandable = (semantics.actions & (AccessibilityAction.Expand |
				AccessibilityAction.Collapse)) != 0 ||
				(semantics.states & AccessibilityState.Expanded) != 0 ||
				(semantics.role == AccessibilityRole.TreeItem && node.children.length > 0);
			var requiredExpansionAction = (semantics.states & AccessibilityState.Expanded) != 0
				? AccessibilityAction.Collapse : AccessibilityAction.Expand;
			if (isExpandable && (semantics.actions & requiredExpansionAction) == 0)
				issues.push(new AccessibilityIssue(node.id.value, "expandable-missing-actions",
					"Expandable items need an action matching their current expanded state"));
			if ((semantics.role == AccessibilityRole.Slider ||
				semantics.role == AccessibilityRole.ProgressBar) &&
				(semantics.numericMinimum > semantics.numericMaximum ||
				semantics.numericValue < semantics.numericMinimum ||
				semantics.numericValue > semantics.numericMaximum))
				issues.push(new AccessibilityIssue(node.id.value, "range-out-of-bounds",
					"The numeric value must fall within its declared range"));
			if (semantics.role == AccessibilityRole.CollectionItem &&
				semantics.setSize > 0 && semantics.positionInSet > semantics.setSize)
				issues.push(new AccessibilityIssue(node.id.value, "collection-position-out-of-range",
					"An item's position cannot exceed the collection size"));
			if (semantics.role == AccessibilityRole.Cell)
				auditGridCell(node, semantics, issues);
			if ((semantics.states & AccessibilityState.Modal) != 0 && !node.focusTrap)
				issues.push(new AccessibilityIssue(node.id.value, "modal-without-focus-trap",
					"Modal elements need a focus trap"));
		}
		var effectiveEnabled = ancestorsEnabled && node.enabled &&
			(semantics == null || (semantics.states & AccessibilityState.Disabled) == 0);
		var focusable = node.focusable || semantics != null &&
			(semantics.states & AccessibilityState.Focusable) != 0;
		if (focusable && !effectiveEnabled)
			issues.push(new AccessibilityIssue(node.id.value, "focusable-disabled",
				"Disabled nodes cannot remain focusable"));
		if (node.focusable && node.resolved != null && node.resolved.visible &&
			(node.resolved.width <= 0.0 || node.resolved.height <= 0.0))
			issues.push(new AccessibilityIssue(node.id.value, "empty-focus-bounds",
				"Visible focusable nodes need positive resolved bounds"));
		for (child in node.children)
			inspectNode(child, effectiveEnabled, ids, issues);
	}

	static function hasSelectedTab(node:RenderNode):Bool {
		for (child in node.children) {
			var semantics:Null<Semantics> = cast child.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.TabList)
				continue;
			if (semantics != null && semantics.role == AccessibilityRole.Tab &&
				(semantics.states & AccessibilityState.Selected) != 0)
				return true;
			if (hasSelectedTab(child))
				return true;
		}
		return false;
	}

	static function auditGridCell(node:RenderNode, cell:Semantics,
			issues:Array<AccessibilityIssue>):Void {
		var grid:Null<Semantics> = null;
		var ancestor = node.parent;
		while (ancestor != null && grid == null) {
			var current:RenderNode = cast ancestor;
			var semantics:Null<Semantics> = cast current.semantics;
			if (semantics != null && semantics.role == AccessibilityRole.Grid)
				grid = semantics;
			ancestor = current.parent;
		}
		if (grid == null)
			return;
		var rowSpan = cell.rowSpan == 0 ? 1 : cell.rowSpan;
		var columnSpan = cell.columnSpan == 0 ? 1 : cell.columnSpan;
		var outside = cell.rowIndex >= 0 && grid.rowCount > 0 &&
			(cell.rowIndex >= grid.rowCount || rowSpan > grid.rowCount - cell.rowIndex);
		outside = outside || cell.columnIndex >= 0 && grid.columnCount > 0 &&
			(cell.columnIndex >= grid.columnCount || columnSpan > grid.columnCount - cell.columnIndex);
		if (outside)
			issues.push(new AccessibilityIssue(node.id.value, "grid-cell-out-of-bounds",
				"A grid cell must fit within its declared row and column counts"));
	}

	static function needsName(role:AccessibilityRole):Bool {
		return role == AccessibilityRole.Button || role == AccessibilityRole.Checkbox ||
			role == AccessibilityRole.Radio || role == AccessibilityRole.Link ||
			role == AccessibilityRole.Slider || role == AccessibilityRole.TextField ||
			role == AccessibilityRole.Switch || role == AccessibilityRole.Tab ||
			role == AccessibilityRole.ComboBox ||
			role == AccessibilityRole.ProgressBar;
	}

	static function isBlank(value:String):Bool {
		for (index in 0...value.length) {
			var code = value.charCodeAt(index);
			if (code != 9 && code != 10 && code != 11 && code != 12 && code != 13 &&
				code != 32 && code != 160 && code != 0x3000)
				return false;
		}
		return true;
	}
}
