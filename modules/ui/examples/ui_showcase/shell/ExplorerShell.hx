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
		var main = new Column("main-content", [explorer.keyed("page", pageScroll)], mainStyle);
		var inspector:Null<View> = null;
		if (explorer.state.inspector.open && explorer.width >= 880.0)
			inspector = explorer.buildInspector();
		var shellStyle = new LayoutStyle();
		shellStyle.width = LayoutAxis.grow();
		shellStyle.height = LayoutAxis.grow();
		shellStyle.background = explorer.paletteBackground();
		var shell = new AppShell("app-shell", main, TopBar.build(explorer),
			CatalogSidebar.build(explorer), inspector, shellStyle);
		return new DefaultTextStyle(shell, TextStyleOverride.text(15.0));
	}
}
