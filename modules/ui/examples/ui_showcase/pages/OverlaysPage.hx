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
			explorer.keyed("heading", explorer.text("Tabs", explorer.paletteText())),
			explorer.keyed("tabs", new Tabs("demo-tabs", [
				new TabItem("preview", "Preview", explorer.text(
					"Selected content is built lazily and keeps a stable keyed identity.", explorer.paletteMuted())),
				new TabItem("details", "Details", explorer.text(
					"Tab navigation supports arrow keys and visible focus.", explorer.paletteMuted())),
				new TabItem("disabled", "Disabled", explorer.text(
					"This tab is not enabled.", explorer.paletteMuted()), false)
			], explorer.selectedTab, function(value) { explorer.selectedTab = value; }))
		])));
		items.push(explorer.keyed("overlay-actions", explorer.panel("overlay-actions", [
			explorer.keyed("heading", explorer.text("Open an overlay", explorer.paletteText())),
			explorer.keyed("actions", new Row("overlay-buttons", [
				explorer.keyed("dialog", explorer.button("Show dialog", "show-dialog", function() {
					explorer.showDialog = true;
				})),
				explorer.keyed("popup", explorer.button("Show popup", "show-popup", function() {
					explorer.showPopup = true;
				})),
				explorer.keyed("menu", explorer.button("Show menu", "show-menu", function() {
					explorer.showMenu = true;
				}))
			], explorer.rowStyle(10.0))),
			explorer.keyed("menu-state", explorer.text(explorer.menuSelection, explorer.paletteMuted())),
			explorer.keyed("tooltip", new Tooltip("tooltip-demo",
				explorer.button("Hover for tooltip", "tooltip-anchor", function() {}),
				explorer.text("Tooltip content is positioned in a Stack layer.", explorer.paletteText()),
				0.0, -32.0))
		])));
	}
}
