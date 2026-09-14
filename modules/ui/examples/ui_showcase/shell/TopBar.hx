package shell;

import LayoutAlignment;
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
		style.childAlignY = LayoutAlignment.Center;
		style.padding = new Insets(22.0, 0.0, 22.0, 0.0);
		style.childGap = 12.0;
		style.background = explorer.paletteSidebar();
		return new Row("top-bar", [
			explorer.keyed("brand-mark", explorer.text("NK", UiExplorer.color(0.31, 0.91, 0.72))),
			explorer.keyed("brand", explorer.text("NativeKit UI Explorer", explorer.paletteText())),
			explorer.keyed("space", new Spacer("top-spacer", LayoutAxis.grow(), LayoutAxis.fit())),
			explorer.keyed("platform", explorer.text(explorer.platformLabel, explorer.paletteMuted())),
			explorer.keyed("theme", explorer.button(explorer.lightTheme ? "Light theme" : "Dark theme",
				"theme-toggle", function() {
					explorer.lightTheme = !explorer.lightTheme;
					explorer.context.setTheme(UiExplorer.makeTheme(explorer.lightTheme));
				})),
			explorer.keyed("inspect", explorer.button(explorer.inspectorOpen ? "Hide inspector" : "Inspect",
				"inspector-toggle", function() {
					explorer.inspectorOpen = !explorer.inspectorOpen;
				}))
		], style);
	}
}
