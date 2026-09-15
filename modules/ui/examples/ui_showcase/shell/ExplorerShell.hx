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
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.widgets.SplitView;
import nativekit.ui.widgets.SplitViewOptions;

/** Persistent three-column shell around the selected Explorer page. */
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
			var splitOptions = new SplitViewOptions();
			splitOptions.secondaryExtent = explorer.state.inspector.width;
			splitOptions.minimumExtent = 220.0;
			splitOptions.maximumExtent = 420.0;
			splitOptions.dividerExtent = 8.0;
			var dividerStyle = new LayoutStyle();
			dividerStyle.background = explorer.state.lightTheme
				? UiExplorer.color(0.78, 0.82, 0.89) : UiExplorer.color(0.13, 0.17, 0.24);
			splitOptions.dividerStyle = dividerStyle;
			splitOptions.onResize = function(value) {
				explorer.state.inspector.width = value;
			};
			main = new SplitView("main-inspector", main, explorer.buildInspector(), splitOptions);
		}
		var shellStyle = new LayoutStyle();
		shellStyle.width = LayoutAxis.grow();
		shellStyle.height = LayoutAxis.grow();
		shellStyle.background = explorer.paletteBackground();
		var shell = new AppShell("app-shell", main, TopBar.build(explorer),
			CatalogSidebar.build(explorer), null, shellStyle);
		return new DefaultTextStyle(shell, TextStyleOverride.text(15.0));
	}
}
