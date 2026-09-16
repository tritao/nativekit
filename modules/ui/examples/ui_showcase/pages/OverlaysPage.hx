package pages;

import UiExplorer;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.TabItem;
import nativekit.ui.widgets.Tabs;
import nativekit.ui.widgets.Tooltip;

/** Tabs and transient overlay demonstrations. */
class OverlaysPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Navigation & Overlays",
			"Tabs, menus, popups, tooltips and dialogs share focus, clipping and z-order rules.");
		items.push(explorer.keyed("tabs-card", explorer.panel("tabs-card", [
			explorer.keyed("heading", explorer.heading("Tabs")),
			explorer.keyed("tabs", new Tabs("demo-tabs", [
				new TabItem("preview", "Preview", explorer.caption(
					"Selected content is built lazily and keeps a stable keyed identity.")),
				new TabItem("details", "Details", explorer.caption(
					"Tab navigation supports arrow keys and visible focus.")),
				new TabItem("disabled", "Disabled", explorer.caption(
					"This tab is not enabled."), false)
			], explorer.state.controls.selectedTab, function(value) { explorer.state.controls.selectedTab = value; }))
		])));
		items.push(explorer.keyed("overlay-actions", explorer.panel("overlay-actions", [
			explorer.keyed("heading", explorer.heading("Open an overlay")),
			explorer.keyed("actions", new Row("overlay-buttons", [
				explorer.keyed("dialog", explorer.button("Show dialog", "show-dialog", function() {
					explorer.state.overlays.dialogOpen = true;
				})),
				explorer.keyed("popup", explorer.button("Show popup", "show-popup", function() {
					for (record in explorer.context.inspect())
						if (record.focusable && record.label == "Show popup") {
							explorer.state.overlays.popupAnchorX = record.bounds.x;
							explorer.state.overlays.popupAnchorTop = record.bounds.y;
							explorer.state.overlays.popupAnchorBottom =
								record.bounds.y + record.bounds.height;
							break;
						}
					explorer.state.overlays.popupOpen = true;
				})),
				explorer.keyed("menu", explorer.button("Show menu", "show-menu", function() {
					for (record in explorer.context.inspect())
						if (record.focusable && record.label == "Show menu") {
							explorer.state.overlays.menuAnchorX = record.bounds.x;
							explorer.state.overlays.menuAnchorTop = record.bounds.y;
							explorer.state.overlays.menuAnchorBottom =
								record.bounds.y + record.bounds.height;
							break;
						}
					explorer.state.overlays.menuOpen = true;
				}))
			], explorer.rowStyle(10.0))),
			explorer.keyed("menu-state", explorer.caption(explorer.state.controls.menuSelection)),
			explorer.keyed("tooltip", new Tooltip("tooltip-demo",
				explorer.button("Hover for tooltip", "tooltip-anchor", function() {}),
				explorer.caption("Tooltip content is positioned in a Stack layer."),
				0.0, -32.0))
		])));
	}
}
