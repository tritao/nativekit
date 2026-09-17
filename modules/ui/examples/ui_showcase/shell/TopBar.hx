package shell;

import LayoutAlignmentY;
import LayoutAxis;
import LayoutDistribution;
import LayoutDirection;
import LayoutStyle;
import Insets;
import UiExplorer;
import nativekit.ui.icons.IconName;
import nativekit.ui.widgets.Button;
import nativekit.ui.core.View;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.WindowChrome;
import NativeKit.WindowDecorationRegionKind;

/** Top-level branding and global Explorer actions. */
class TopBar {
	public static function build(explorer:UiExplorer):View {
		var style = new LayoutStyle();
		style.width = LayoutAxis.grow();
		style.height = LayoutAxis.fixed(66.0);
		style.direction = LayoutDirection.LeftToRight;
		style.childAlignY = LayoutAlignmentY.Center;
		style.childDistribution = LayoutDistribution.SpaceBetween;
		style.padding = new Insets(22.0, 0.0, 22.0, 0.0);
		style.background = explorer.paletteSidebar();
		var brand = new Row("top-brand", [
			explorer.keyed("brand-mark", explorer.text("HX",
				explorer.context.buildContext.theme.accent)),
			explorer.keyed("brand", explorer.heading("Haxeon UI Explorer"))
		], clusterStyle(12.0));
		var themeButton = explorer.button(explorer.state.lightTheme ? "Dark mode" : "Light mode",
			"theme-toggle", function() {
			explorer.toggleTheme();
			});
		themeButton.leadingIcon = explorer.state.lightTheme ? IconName.Moon : IconName.Sun;
		var actions:Array<KeyedView> = [explorer.keyed("theme", new WindowChrome("theme-client",
			WindowDecorationRegionKind.Client, themeButton))];
		if (explorer.width >= 880.0)
			actions.push(explorer.keyed("inspect", new WindowChrome("inspect-client",
				WindowDecorationRegionKind.Client, inspectButton(explorer))));
		return new WindowChrome("top-bar-drag", WindowDecorationRegionKind.Drag, new Row("top-bar", [
			explorer.keyed("brand-cluster", brand),
			explorer.keyed("action-cluster", new Row("top-actions", actions, clusterStyle(8.0)))
		], style));
	}

	static function inspectButton(explorer:UiExplorer):Button {
		var button = explorer.button(
				explorer.state.inspector.picking ? "Cancel inspect" : "Inspect",
				"inspector-toggle", function() {
					if (explorer.state.inspector.picking) {
						explorer.state.inspector.picking = false;
						explorer.state.inspector.hoveredNodeId = 0;
					} else {
						explorer.state.inspector.open = true;
						explorer.state.inspector.picking = true;
					}
				}, explorer.state.inspector.open || explorer.state.inspector.picking);
		button.leadingIcon = IconName.Inspect;
		return button;
	}

	static function clusterStyle(gap:Float):LayoutStyle {
		var style = new LayoutStyle();
		style.width = LayoutAxis.fit();
		style.height = LayoutAxis.fit();
		style.direction = LayoutDirection.LeftToRight;
		style.childAlignY = LayoutAlignmentY.Center;
		style.childGap = gap;
		return style;
	}
}
