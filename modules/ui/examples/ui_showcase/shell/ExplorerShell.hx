package shell;

import Insets;
import LayoutAxis;
import LayoutStyle;
import UiExplorer;
import shell.CatalogSidebar;
import shell.TopBar;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.View;
import nativekit.ui.widgets.AppShell;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.DefaultTextStyle;
import nativekit.ui.widgets.SplitSide;
import nativekit.ui.widgets.SplitView;
import nativekit.ui.widgets.SplitViewOptions;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;

/** Persistent resizable catalog/content/inspector shell around Explorer pages. */
class ExplorerShell {
	public static function build(explorer:UiExplorer):View {
		var mainStyle = new LayoutStyle();
		mainStyle.width = LayoutAxis.grow();
		mainStyle.height = LayoutAxis.grow();
		mainStyle.padding = new Insets(22.0, 18.0, 22.0, 18.0);
		var scrollStyle = new LayoutStyle();
		scrollStyle.width = LayoutAxis.grow();
		scrollStyle.height = LayoutAxis.grow();
		scrollStyle.clipVertical = true;
		var pageScroll = new ScrollView("page-scroll", explorer.buildPage(), scrollStyle,
			ScrollAxis.Vertical);
		var main:View = new Column("main-content", [explorer.keyed("page", pageScroll)], mainStyle);
		if (explorer.state.inspector.open && explorer.width >= 880.0) {
			var inspectorOptions = new SplitViewOptions();
			inspectorOptions.resizableSide = SplitSide.Trailing;
			inspectorOptions.extent = explorer.state.inspector.width;
			inspectorOptions.minimumExtent = 220.0;
			inspectorOptions.maximumExtent = 420.0;
			inspectorOptions.dividerExtent = 8.0;
			var dividerStyle = new LayoutStyle();
			dividerStyle.background = explorer.state.lightTheme
				? UiExplorer.color(0.78, 0.82, 0.89) : UiExplorer.color(0.13, 0.17, 0.24);
			inspectorOptions.dividerStyle = dividerStyle;
			inspectorOptions.onResize = function(value) {
				explorer.state.inspector.width = value;
			};
			main = new SplitView("main-inspector", main, explorer.buildInspector(), inspectorOptions);
		}

		var catalogOptions = new SplitViewOptions();
		catalogOptions.resizableSide = SplitSide.Leading;
		catalogOptions.extent = explorer.state.catalogWidth > 0.0
			? explorer.state.catalogWidth : (explorer.width < 760.0 ? 176.0 : 212.0);
		catalogOptions.minimumExtent = explorer.width < 760.0 ? 160.0 : 176.0;
		catalogOptions.maximumExtent = 300.0;
		catalogOptions.dividerExtent = 8.0;
		var catalogDividerStyle = new LayoutStyle();
		catalogDividerStyle.background = explorer.state.lightTheme
			? UiExplorer.color(0.78, 0.82, 0.89) : UiExplorer.color(0.13, 0.17, 0.24);
		catalogOptions.dividerStyle = catalogDividerStyle;
		catalogOptions.onResize = function(value) {
			explorer.state.catalogWidth = value;
		};
		var shellContent:View = new SplitView("catalog-content",
			CatalogSidebar.build(explorer), main, catalogOptions);

		var shellStyle = new LayoutStyle();
		shellStyle.width = LayoutAxis.grow();
		shellStyle.height = LayoutAxis.grow();
		shellStyle.background = explorer.paletteBackground();
		var shell = new AppShell("app-shell", shellContent, TopBar.build(explorer),
			null, null, shellStyle);
		return new DefaultTextStyle(shell, TextStyleOverride.text(15.0));
	}
}
