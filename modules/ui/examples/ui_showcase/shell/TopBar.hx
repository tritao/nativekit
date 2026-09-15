package shell;

import LayoutAlignmentY;
import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import Insets;
import UiExplorer;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Spacer;

/** Top-level branding and global Explorer actions. */
class TopBar {
	public static function build(explorer:UiExplorer):Row {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(66.0);
		style.direction = LayoutDirection.LeftToRight;
		style.childAlignY = LayoutAlignmentY.Center;
		style.padding = new Insets(22.0, 0.0, 22.0, 0.0);
		style.childGap = 12.0;
		style.background = explorer.paletteSidebar();
		var children:Array<KeyedView> = [
			explorer.keyed("brand-mark", explorer.text("NK", UiExplorer.color(0.31, 0.91, 0.72))),
			explorer.keyed("brand", explorer.heading("NativeKit UI Explorer")),
			explorer.keyed("space", new Spacer("top-spacer", LayoutAxis.grow(), LayoutAxis.fit())),
			explorer.keyed("platform", explorer.caption(explorer.platformLabel)),
			explorer.keyed("theme", explorer.button(explorer.state.lightTheme ? "Light theme" : "Dark theme",
				"theme-toggle", function() {
					explorer.state.lightTheme = !explorer.state.lightTheme;
					explorer.context.setTheme(UiExplorer.makeTheme(explorer.state.lightTheme));
				})),
		];
		if (explorer.width >= 880.0)
			children.push(explorer.keyed("inspect", explorer.button(
				explorer.state.inspector.open ? "Hide inspector" : "Inspect",
				"inspector-toggle", function() {
					explorer.state.inspector.open = !explorer.state.inspector.open;
			})));
		return new Row("top-bar", children, style);
	}
}
