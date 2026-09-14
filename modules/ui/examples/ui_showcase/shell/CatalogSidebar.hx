package shell;

import Insets;
import LayoutAxis;
import LayoutStyle;
import TextStyle;
import UiExplorer;
import nativekit.ui.widgets.Button;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.TextField;

/** Search and navigation surface for the Explorer's page catalog. */
class CatalogSidebar {
	public static function build(explorer:UiExplorer):Column {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fixed(explorer.width < 760.0 ? 176.0 : 212.0);
		style.height = LayoutAxis.grow();
		style.padding = new Insets(14.0, 18.0, 14.0, 18.0);
		style.childGap = 8.0;
		style.background = explorer.paletteSidebar();
		var children:Array<KeyedView> = [
			explorer.keyed("catalog-label", explorer.text("COMPONENT CATALOG", explorer.paletteMuted()))
		];
		var searchStyle = new LayoutStyle();
		searchStyle.width = LayoutAxis.grow();
		searchStyle.height = LayoutAxis.fixed(38.0);
		searchStyle.padding = new Insets(9.0, 7.0, 9.0, 7.0);
		searchStyle.background = explorer.lightTheme
			? UiExplorer.color(0.91, 0.93, 0.97)
			: UiExplorer.color(0.09, 0.12, 0.18);
		var search = new TextField("catalog-search", explorer.searchText, function(value) {
			explorer.searchText = value;
		}, searchStyle, "Search components", new TextStyle(14.0), explorer.paletteText());
		children.push(explorer.keyed("search", search));
		var navItems:Array<KeyedView> = [explorer.keyed("group-start",
			explorer.text("START HERE", explorer.paletteMuted()))];
		appendNav(explorer, navItems, "overview", "Overview");
		navItems.push(explorer.keyed("group-components",
			explorer.text("COMPONENTS", explorer.paletteMuted())));
		appendNav(explorer, navItems, "controls", "Controls");
		appendNav(explorer, navItems, "text", "Text & Input");
		appendNav(explorer, navItems, "layout", "Layout");
		appendNav(explorer, navItems, "lists", "Scrolling & Data");
		appendNav(explorer, navItems, "overlays", "Navigation & Overlays");
		appendNav(explorer, navItems, "gestures", "Gestures & Motion");
		navItems.push(explorer.keyed("group-developer",
			explorer.text("DEVELOPER TOOLS", explorer.paletteMuted())));
		appendNav(explorer, navItems, "graphics", "Graphics Lab");
		var navStyle = new LayoutStyle();
		navStyle.width = LayoutAxis.grow();
		navStyle.height = LayoutAxis.fit();
		navStyle.childGap = 6.0;
		var navScrollStyle = new LayoutStyle();
		navScrollStyle.width = LayoutAxis.grow();
		navScrollStyle.height = LayoutAxis.grow();
		navScrollStyle.clipVertical = true;
		children.push(explorer.keyed("catalog-navigation", new ScrollView("catalog-navigation-scroll",
			new Column("catalog-navigation-items", navItems, navStyle), navScrollStyle,
			ScrollAxis.Vertical)));
		children.push(explorer.keyed("catalog-foot",
			explorer.text("Haxe composition\nNative layout + render", explorer.paletteMuted())));
		return new Column("component-catalog", children, style);
	}

	static function appendNav(explorer:UiExplorer, children:Array<KeyedView>, key:String,
			label:String):Void {
		if (explorer.searchText.length > 0 && !UiExplorer.containsInsensitive(label, explorer.searchText))
			return;
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(36.0);
		style.padding = new Insets(10.0, 8.0, 10.0, 8.0);
		style.background = explorer.lightTheme
			? UiExplorer.color(0.87, 0.90, 0.95)
			: UiExplorer.color(0.075, 0.10, 0.16);
		var item = new Button(label, style, function() {
			explorer.selectedPage = key;
			explorer.selectedNodeId = 0;
			explorer.hoveredNodeId = 0;
			explorer.inspectorTab = "preview";
		}, "nav-" + key);
		item.selected = explorer.selectedPage == key;
		children.push(explorer.keyed("nav-" + key, item));
	}
}
