package shell;

import Insets;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import UiExplorer;
import shell.CatalogSidebar;
import shell.TopBar;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.DefaultTextStyle;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.ScrollAxis;
import nativekit.ui.widgets.ScrollView;
import nativekit.ui.core.TextStyleOverride;
import nativekit.ui.core.View;

/** Persistent three-column shell around the selected Explorer page. */
class ExplorerShell {
	public static function build(explorer:UiExplorer):View {
		var bodyStyle = new LayoutStyle();
		bodyStyle.width = LayoutAxis.grow();
		bodyStyle.height = LayoutAxis.grow();
		bodyStyle.direction = LayoutDirection.LeftToRight;
		bodyStyle.childGap = 0.0;
		var bodyChildren:Array<KeyedView> = [
			explorer.keyed("catalog", CatalogSidebar.build(explorer))
		];
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
		bodyChildren.push(explorer.keyed("main", new Column("main-content",
			[explorer.keyed("page", pageScroll)], mainStyle)));
		if (explorer.state.inspector.open && explorer.width >= 880.0)
			bodyChildren.push(explorer.keyed("inspector", explorer.buildInspector()));
		var body = new Row("workspace", bodyChildren, bodyStyle);
		var shellStyle = new LayoutStyle();
		shellStyle.width = LayoutAxis.grow();
		shellStyle.height = LayoutAxis.grow();
		shellStyle.background = explorer.paletteBackground();
		var shell = new Column("app-shell", [
			explorer.keyed("top", TopBar.build(explorer)),
			explorer.keyed("workspace", body)
		], shellStyle);
		return new DefaultTextStyle(shell, TextStyleOverride.text(15.0));
	}
}
