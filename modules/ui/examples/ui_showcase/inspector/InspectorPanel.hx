package inspector;

import LayoutAxis;
import LayoutStyle;
import Insets;
import UiExplorer;
import nativekit.ui.debug.AccessibilityIssue;
import nativekit.ui.debug.UiNodeSnapshot;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;
import inspector.InspectionOverlay;
import inspector.WidgetDocsRegistry;

/** Inspector drawer: selected render node, state, semantics, and ancestry. */
class InspectorPanel {
	public static function build(explorer:UiExplorer):Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(270.0);
		style.height = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		style.background = explorer.paletteSidebar();
		var records:Array<UiNodeSnapshot> = explorer.context.inspect();
		var issues:Array<AccessibilityIssue> = explorer.context.auditAccessibility();
		var selected = chooseInspectionRecord(explorer, records);
		var children:Array<KeyedView> = [
			explorer.keyed("title", explorer.text("INSPECT", explorer.paletteText())),
			explorer.keyed("subtitle", explorer.text("Hover to preview · click to pin", explorer.paletteMuted())),
			explorer.keyed("tab-row-one", new Row("inspector-tab-row-one", [
				explorer.keyed("preview", tabButton(explorer, "Preview", "preview")),
				explorer.keyed("state", tabButton(explorer, "State", "state"))
			], explorer.rowStyle(6.0))),
			explorer.keyed("tab-row-two", new Row("inspector-tab-row-two", [
				explorer.keyed("semantics", tabButton(explorer, "Semantics", "semantics")),
				explorer.keyed("tree", tabButton(explorer, "Tree", "tree"))
			], explorer.rowStyle(6.0)))
		];
		var contentStyle = new LayoutStyle();
		contentStyle.width = LayoutAxis.grow();
		contentStyle.height = LayoutAxis.grow();
		contentStyle.clipVertical = true;
		var body = new ScrollView("inspector-content-scroll",
			buildContent(explorer, selected, records, issues), contentStyle, ScrollAxis.Vertical);
		children.push(explorer.keyed("content", body));
		var auditColor = issues.length == 0
			? UiExplorer.color(0.35, 0.85, 0.69)
			: UiExplorer.color(0.96, 0.58, 0.31);
		children.push(explorer.keyed("audit-summary", explorer.text(issues.length == 0
			? "Accessibility audit · clean" : 'Accessibility audit · ${issues.length} issue(s)',
			auditColor)));
		return new Column("inspector-drawer", children, style);
	}

	static function tabButton(explorer:UiExplorer, label:String, tab:String):Button {
		var item = explorer.button(label, "inspector-tab-" + tab,
			function() { explorer.inspectorTab = tab; }, explorer.inspectorTab == tab);
		item.style.width = LayoutAxis.grow();
		item.style.height = LayoutAxis.fixed(30.0);
		item.style.padding = new Insets(7.0, 5.0, 7.0, 5.0);
		return item;
	}

	static function buildContent(explorer:UiExplorer, record:Null<UiNodeSnapshot>,
			records:Array<UiNodeSnapshot>, issues:Array<AccessibilityIssue>):Column {
		var children:Array<KeyedView> = [];
		if (record == null) {
			children.push(explorer.keyed("empty", explorer.text(
				"Move over a widget in the preview to inspect it. Click to keep it selected.",
				explorer.paletteMuted())));
			return new Column("inspector-empty-content", children);
		}
		var label = record.label == null || record.label.length == 0 ? "Unnamed node" : record.label;
		children.push(explorer.keyed("selected-label", explorer.text(label, explorer.paletteText())));
		children.push(explorer.keyed("selected-id", explorer.text(
			'#${record.id} · ${WidgetDocsRegistry.visualName(record.visualKind)} · ${WidgetDocsRegistry.roleName(record.role)}',
			explorer.paletteMuted())));
		switch explorer.inspectorTab {
			case "state":
				children.push(explorer.keyed("state-heading", explorer.text("LIVE STATE", explorer.paletteText())));
				children.push(explorer.keyed("state-focus", explorer.text(
					'focused=${record.focused}  focusable=${record.focusable}', explorer.paletteMuted())));
				children.push(explorer.keyed("state-pointer", explorer.text(
					'hovered=${record.hovered}  pressed=${record.pressed}', explorer.paletteMuted())));
				children.push(explorer.keyed("state-enabled", explorer.text(
					'enabled=${record.enabled}  visible=${record.visible}', explorer.paletteMuted())));
				children.push(explorer.keyed("state-value", explorer.text(
					'value=${record.value == null ? "(none)" : record.value}', explorer.paletteMuted())));
				children.push(explorer.keyed("state-semantic", explorer.text(
					'semantic states: ${WidgetDocsRegistry.semanticStateNames(record.semanticStates)}',
					explorer.paletteMuted())));
				children.push(explorer.keyed("state-bounds", explorer.text(
					'bounds: ${WidgetDocsRegistry.rectText(record.bounds)}\nclip: ${WidgetDocsRegistry.rectText(record.clipBounds)}\nz-order: ${record.zIndex}',
					explorer.paletteMuted())));
			case "semantics":
				children.push(explorer.keyed("sem-heading", explorer.text("ACCESSIBILITY", explorer.paletteText())));
				children.push(explorer.keyed("sem-role", explorer.text(
					'role: ${WidgetDocsRegistry.roleName(record.role)}', explorer.paletteMuted())));
				children.push(explorer.keyed("sem-label", explorer.text('label: ${label}', explorer.paletteMuted())));
				children.push(explorer.keyed("sem-value", explorer.text(
					'value: ${record.value == null ? "(none)" : record.value}', explorer.paletteMuted())));
				children.push(explorer.keyed("sem-states", explorer.text(
					'states: ${WidgetDocsRegistry.semanticStateNames(record.semanticStates)}',
					explorer.paletteMuted())));
				children.push(explorer.keyed("sem-actions", explorer.text(
					'actions: ${WidgetDocsRegistry.actionNames(record.actions)}', explorer.paletteMuted())));
				var nodeIssues:Array<String> = [];
				for (issue in issues)
					if (issue.nodeId == record.id)
						nodeIssues.push(issue.code + ": " + issue.message);
				children.push(explorer.keyed("sem-audit-heading",
					explorer.text("AUDIT FINDINGS", explorer.paletteText())));
				children.push(explorer.keyed("sem-audit", explorer.text(nodeIssues.length == 0
					? "No accessibility issues for this node." : nodeIssues.join("\n"),
					nodeIssues.length == 0 ? UiExplorer.color(0.35, 0.85, 0.69)
						: UiExplorer.color(0.96, 0.58, 0.31))));
			case "tree":
				children.push(explorer.keyed("tree-heading", explorer.text("ANCESTRY", explorer.paletteText())));
				for (line in treeAncestry(record, records))
					children.push(explorer.keyed("ancestor-" + children.length,
						explorer.text(line, explorer.paletteMuted())));
				children.push(explorer.keyed("children-heading",
					explorer.text("CHILD NODES", explorer.paletteText())));
				var shown = 0;
				for (child in records)
					if (child.parentId == record.id && shown < 8) {
						children.push(explorer.keyed('child-${shown}',
							explorer.text(treeNodeText(child), explorer.paletteMuted())));
						shown++;
					}
				if (shown == 0)
					children.push(explorer.keyed("tree-leaf",
						explorer.text("No child render nodes.", explorer.paletteMuted())));
			default:
				var synopsis = WidgetDocsRegistry.describe(record.role);
				children.push(explorer.keyed("preview-heading", explorer.text("WHAT THIS IS", explorer.paletteText())));
				children.push(explorer.keyed("preview-description",
					explorer.text(synopsis.description, explorer.paletteMuted())));
				children.push(explorer.keyed("behavior-heading", explorer.text("BEHAVIOR", explorer.paletteText())));
				children.push(explorer.keyed("behavior-description",
					explorer.text(synopsis.behavior, explorer.paletteMuted())));
				children.push(explorer.keyed("code-heading", explorer.text("HAXE", explorer.paletteText())));
				children.push(explorer.keyed("code-snippet",
					explorer.text(synopsis.code, UiExplorer.color(0.48, 0.82, 0.75))));
				children.push(explorer.keyed("geometry-heading",
					explorer.text("RESOLVED GEOMETRY", explorer.paletteText())));
				children.push(explorer.keyed("geometry", explorer.text(
					'bounds ${WidgetDocsRegistry.rectText(record.bounds)}\nclip ${WidgetDocsRegistry.rectText(record.clipBounds)}\nz ${record.zIndex}',
					explorer.paletteMuted())));
		}
		return new Column("inspector-content-" + explorer.inspectorTab, children,
			explorer.panelStyle());
	}

	static function chooseInspectionRecord(explorer:UiExplorer,
			records:Array<UiNodeSnapshot>):Null<UiNodeSnapshot> {
		if (explorer.hoveredNodeId != 0) {
			var hovered = InspectionOverlay.findSnapshot(records, explorer.hoveredNodeId);
			if (hovered != null)
				return hovered;
		}
		if (explorer.selectedNodeId != 0) {
			var selected = InspectionOverlay.findSnapshot(records, explorer.selectedNodeId);
			if (selected != null)
				return selected;
		}
		for (record in records)
			if (record.focused && InspectionOverlay.isRecordInPreview(explorer, record))
				return record;
		for (record in records)
			if (record.visible && record.focusable && record.role >= 0 &&
					InspectionOverlay.isRecordInPreview(explorer, record))
				return record;
		return null;
	}

	static function treeAncestry(record:UiNodeSnapshot,
			records:Array<UiNodeSnapshot>):Array<String> {
		var chain:Array<UiNodeSnapshot> = [record];
		var parentId = record.parentId;
		while (parentId != 0 && chain.length < 32) {
			var parent = InspectionOverlay.findSnapshot(records, parentId);
			if (parent == null)
				break;
			chain.push(parent);
			parentId = parent.parentId;
		}
		chain.reverse();
		var result:Array<String> = [];
		for (index in 0...chain.length) {
			var indent = "";
			for (_ in 0...index)
				indent += "  ";
			result.push(indent + treeNodeText(chain[index]));
		}
		return result;
	}

	static function treeNodeText(record:UiNodeSnapshot):String {
		var label = record.label == null || record.label.length == 0 ? "" : ' · ${record.label}';
		return '#${record.id} ${WidgetDocsRegistry.visualName(record.visualKind)}${label} · ${WidgetDocsRegistry.roleName(record.role)} · z=${record.zIndex}';
	}
}
