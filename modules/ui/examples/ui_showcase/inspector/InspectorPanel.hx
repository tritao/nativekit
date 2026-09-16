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
import components.CodeSample;
import components.PropertyRow;

/** Inspector drawer: selected render node, state, semantics, and ancestry. */
class InspectorPanel {
	public static function build(explorer:UiExplorer):Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.grow();
		style.padding = new Insets(12.0, 12.0, 12.0, 12.0);
		style.childGap = 8.0;
		style.background = explorer.paletteSidebar();
		var records:Array<UiNodeSnapshot> = explorer.context.inspect();
		var issues:Array<AccessibilityIssue> = explorer.context.auditAccessibility();
		var selected = chooseInspectionRecord(explorer, records);
		var children:Array<KeyedView> = [
			explorer.keyed("title", explorer.heading("INSPECT")),
			explorer.keyed("subtitle", explorer.caption("Hover to preview · click to pin")),
			explorer.keyed("tab-row-one", new Row("inspector-tab-row-one", [
				explorer.keyed("preview", tabButton(explorer, "Preview", "preview")),
				explorer.keyed("state", tabButton(explorer, "State", "state"))
			], explorer.rowStyle(6.0))),
			explorer.keyed("tab-row-two", new Row("inspector-tab-row-two", [
				explorer.keyed("semantics", tabButton(explorer, "Semantics", "semantics")),
				explorer.keyed("tree", tabButton(explorer, "Tree", "tree"))
			], explorer.rowStyle(6.0)))
		];
		children.push(explorer.keyed("tab-row-three", new Row("inspector-tab-row-three", [
			explorer.keyed("layout", tabButton(explorer, "Layout", "layout")),
			explorer.keyed("style", tabButton(explorer, "Style", "style"))
		], explorer.rowStyle(6.0))));
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
			function() { explorer.state.inspector.tab = tab; }, explorer.state.inspector.tab == tab);
		item.style.width = LayoutAxis.grow();
		item.style.height = LayoutAxis.fixed(30.0);
		item.style.padding = new Insets(7.0, 5.0, 7.0, 5.0);
		return item;
	}

	static function buildContent(explorer:UiExplorer, record:Null<UiNodeSnapshot>,
			records:Array<UiNodeSnapshot>, issues:Array<AccessibilityIssue>):Column {
		var children:Array<KeyedView> = [];
		if (record == null) {
			children.push(explorer.keyed("empty", explorer.caption(
				"Move over a widget in the preview to inspect it. Click to keep it selected.")));
			return new Column("inspector-empty-content", children);
		}
		var label = record.label == null || record.label.length == 0 ? "Unnamed node" : record.label;
		children.push(explorer.keyed("selected-label", explorer.heading(label)));
		children.push(explorer.keyed("selected-id", explorer.caption(
			'#${record.id} · ${WidgetDocsRegistry.visualName(record.visualKind)} · ${WidgetDocsRegistry.roleName(record.role)}'
		)));
		switch explorer.state.inspector.tab {
			case "state":
				children.push(explorer.keyed("state-heading", explorer.label("LIVE STATE")));
				children.push(PropertyRow.build("state-focus",
					'focused=${record.focused}  focusable=${record.focusable}'));
				children.push(PropertyRow.build("state-pointer",
					'hovered=${record.hovered}  pressed=${record.pressed}'));
				children.push(PropertyRow.build("state-enabled",
					'enabled=${record.enabled}  visible=${record.visible}'));
				children.push(PropertyRow.build("state-value",
					'value=${record.value == null ? "(none)" : record.value}'));
				children.push(PropertyRow.build("state-semantic",
					'semantic states: ${WidgetDocsRegistry.semanticStateNames(record.semanticStates)}'));
				children.push(PropertyRow.build("state-bounds",
					'bounds: ${WidgetDocsRegistry.rectText(record.bounds)}\nclip: ${WidgetDocsRegistry.rectText(record.clipBounds)}\nz-order: ${record.zIndex}'));
			case "semantics":
				children.push(explorer.keyed("sem-heading", explorer.label("ACCESSIBILITY")));
				children.push(PropertyRow.build("sem-role",
					'role: ${WidgetDocsRegistry.roleName(record.role)}'));
				children.push(PropertyRow.build("sem-label", 'label: ${label}'));
				children.push(PropertyRow.build("sem-value",
					'value: ${record.value == null ? "(none)" : record.value}'));
				children.push(PropertyRow.build("sem-states",
					'states: ${WidgetDocsRegistry.semanticStateNames(record.semanticStates)}'));
				children.push(PropertyRow.build("sem-actions",
					'actions: ${WidgetDocsRegistry.actionNames(record.actions)}'));
				var nodeIssues:Array<String> = [];
				for (issue in issues)
					if (issue.nodeId == record.id)
						nodeIssues.push(issue.code + ": " + issue.message);
				children.push(explorer.keyed("sem-audit-heading",
					explorer.label("AUDIT FINDINGS")));
				children.push(explorer.keyed("sem-audit", explorer.text(nodeIssues.length == 0
					? "No accessibility issues for this node." : nodeIssues.join("\n"),
					nodeIssues.length == 0 ? UiExplorer.color(0.35, 0.85, 0.69)
						: UiExplorer.color(0.96, 0.58, 0.31))));
			case "tree":
				children.push(explorer.keyed("tree-heading", explorer.label("ANCESTRY")));
				for (line in treeAncestry(record, records))
					children.push(explorer.keyed("ancestor-" + children.length,
						explorer.caption(line)));
				children.push(explorer.keyed("children-heading",
					explorer.label("CHILD NODES")));
				var shown = 0;
				for (child in records)
				if (child.parentId == record.id && shown < 8) {
					children.push(explorer.keyed('child-${shown}',
						explorer.caption(treeNodeText(child))));
						shown++;
					}
			if (shown == 0)
				children.push(explorer.keyed("tree-leaf",
					explorer.caption("No child render nodes.")));
			case "layout":
				children.push(explorer.keyed("layout-heading", explorer.text("LAYOUT", explorer.paletteText())));
				children.push(PropertyRow.build("layout-bounds",
					'bounds: ${WidgetDocsRegistry.rectText(record.bounds)}\ncontent: ${WidgetDocsRegistry.rectText(record.contentBounds)}',
					explorer.paletteMuted()));
				children.push(PropertyRow.build("layout-clip",
					'clip: ${WidgetDocsRegistry.rectText(record.clipBounds)}\nvisible=${record.visible}  z-order=${record.zIndex}',
					explorer.paletteMuted()));
				children.push(PropertyRow.build("layout-identity",
					'type=${record.styleType == null ? "(none)" : record.styleType}\nparent=${record.parentId}',
					explorer.paletteMuted()));
			case "style":
				children.push(explorer.keyed("style-heading", explorer.text("COMPUTED STYLE", explorer.paletteText())));
				children.push(PropertyRow.build("style-summary",
					'${record.styleEntries.length} properties · ${record.matchingStyleRules.length} matching rules',
					explorer.paletteMuted()));
				var shownProperties = 0;
				for (entry in record.styleEntries) {
					if (shownProperties >= 14)
						break;
					var source = entry.source == null ? "framework default" : entry.source.toString();
					children.push(PropertyRow.build('style-property-$shownProperties',
						'${entry.name}: ${Std.string(entry.value)}\n  source: $source',
						explorer.paletteMuted()));
					shownProperties++;
				}
				children.push(explorer.keyed("style-rules-heading",
					explorer.text("MATCHING RULES", explorer.paletteText())));
				var shownRules = 0;
				for (rule in record.matchingStyleRules) {
					if (shownRules >= 8)
						break;
					children.push(explorer.keyed('style-rule-$shownRules',
						explorer.text(rule.toString(), explorer.paletteMuted())));
					shownRules++;
				}
			if (shownRules == 0)
				children.push(explorer.keyed("style-no-rules",
					explorer.text("No matching stylesheet rules.", explorer.paletteMuted())));
			default:
				var synopsis = WidgetDocsRegistry.describe(record.role);
				children.push(explorer.keyed("preview-heading", explorer.label("WHAT THIS IS")));
				children.push(explorer.keyed("preview-description",
					explorer.caption(synopsis.description)));
				children.push(explorer.keyed("behavior-heading", explorer.label("BEHAVIOR")));
				children.push(explorer.keyed("behavior-description",
					explorer.caption(synopsis.behavior)));
				children.push(explorer.keyed("code-heading", explorer.label("HAXE")));
				children.push(CodeSample.build("code-snippet", synopsis.code,
					UiExplorer.color(0.48, 0.82, 0.75)));
				children.push(explorer.keyed("geometry-heading",
					explorer.label("RESOLVED GEOMETRY")));
				children.push(explorer.keyed("geometry", explorer.caption(
					'bounds ${WidgetDocsRegistry.rectText(record.bounds)}\nclip ${WidgetDocsRegistry.rectText(record.clipBounds)}\nz ${record.zIndex}')));
		}
		return new Column("inspector-content-" + explorer.state.inspector.tab, children,
			explorer.panelStyle());
	}

	static function chooseInspectionRecord(explorer:UiExplorer,
			records:Array<UiNodeSnapshot>):Null<UiNodeSnapshot> {
		if (explorer.state.inspector.hoveredNodeId != 0) {
			var hovered = InspectionOverlay.findSnapshot(records, explorer.state.inspector.hoveredNodeId);
			if (hovered != null)
				return hovered;
		}
		if (explorer.state.inspector.selectedNodeId != 0) {
			var selected = InspectionOverlay.findSnapshot(records, explorer.state.inspector.selectedNodeId);
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
