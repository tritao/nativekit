package shell;

import Insets;
import LayoutAxis;
import LayoutStyle;
import TextStyle;
import UiExplorer;
import ExplorerCatalog;
import ExplorerPage;
import components.SectionHeader;
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
		searchStyle.background = explorer.state.lightTheme
			? UiExplorer.color(0.91, 0.93, 0.97)
			: UiExplorer.color(0.09, 0.12, 0.18);
		var search = new TextField("catalog-search", explorer.state.searchText, function(value) {
			explorer.state.searchText = value;
		}, searchStyle, "Search components", new TextStyle(14.0), explorer.paletteText());
		children.push(explorer.keyed("search", search));
		var navItems:Array<KeyedView> = [];
		var lastGroup:Null<String> = null;
		for (page in ExplorerCatalog.all()) {
			if (explorer.state.searchText.length > 0 &&
					!ExplorerCatalog.matches(page, explorer.state.searchText))
				continue;
			if (lastGroup != page.group) {
				lastGroup = page.group;
				navItems.push(SectionHeader.build("group-" + page.group,
					ExplorerCatalog.groupTitle(page.group), explorer.paletteMuted()));
			}
			appendNav(explorer, navItems, page);
		}
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

	static function appendNav(explorer:UiExplorer, children:Array<KeyedView>,
			page:ExplorerPage):Void {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(36.0);
		style.padding = new Insets(10.0, 8.0, 10.0, 8.0);
		style.background = explorer.state.lightTheme
			? UiExplorer.color(0.87, 0.90, 0.95)
			: UiExplorer.color(0.075, 0.10, 0.16);
		var navigationTitle = explorer.width < 760.0 ? page.compactTitle : page.title;
		var item = new Button(navigationTitle, style, function() {
			explorer.state.selectedPage = page.id;
			explorer.state.inspector.selectedNodeId = 0;
			explorer.state.inspector.hoveredNodeId = 0;
			explorer.state.inspector.tab = "preview";
		}, "nav-" + page.id);
		item.selected = explorer.state.selectedPage == page.id;
		children.push(explorer.keyed("nav-" + page.id, item));
	}
}
