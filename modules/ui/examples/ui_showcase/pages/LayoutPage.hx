package pages;

import LayoutAlignmentX;
import LayoutAlignmentY;
import LayoutAxis;
import LayoutDistribution;
import LayoutDirection;
import LayoutStyle;
import LayoutWrapMode;
import UiExplorer;
import nativekit.ui.core.View;
import nativekit.ui.widgets.Align;
import nativekit.ui.widgets.Column;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Demonstrations of the framework's compositional layout widgets. */
class LayoutPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Layout",
			"Resize the window to watch the native layout transaction reflow these Haxe compositions.");
		var layoutCards:Array<KeyedView> = [
			explorer.keyed("row-column", explorer.panel("row-column", [
				explorer.keyed("heading", explorer.heading("Weighted grow")),
				explorer.keyed("copy", explorer.caption("Relative space with min/max-ready grow policies")),
				explorer.keyed("row", new Row("sample-row", [
					explorer.keyed("one", explorer.colorTile("Navigation · 1", UiExplorer.color(0.24, 0.48, 0.77), 1.0)),
					explorer.keyed("two", explorer.colorTile("Editor · 3", UiExplorer.color(0.39, 0.34, 0.72), 3.0)),
					explorer.keyed("three", explorer.colorTile("Inspector · 1", UiExplorer.color(0.20, 0.58, 0.52), 1.0))
			], explorer.rowStyle(8.0)))
			])),
			explorer.keyed("padding-align", explorer.panel("padding-align", [
				explorer.keyed("heading", explorer.heading("Padding + Align")),
				explorer.keyed("aligned", new Align("centered-content",
					explorer.colorTile("Centered in a padded box", UiExplorer.color(0.30, 0.40, 0.59)),
					LayoutAlignmentX.Center, LayoutAlignmentY.Center, explorer.fixedBoxStyle(270.0, 90.0)))
			]))
		];
		var layoutPrimitives:View;
		if (explorer.width < 900.0) {
			var stackedStyle = new LayoutStyle();
			stackedStyle.width = LayoutAxis.grow();
			stackedStyle.height = LayoutAxis.fit();
			stackedStyle.childGap = 14.0;
			layoutPrimitives = new Column("layout-primitives", layoutCards, stackedStyle);
		} else
			layoutPrimitives = new Row("layout-primitives", layoutCards, explorer.rowStyle(14.0));
		items.push(explorer.keyed("layout-primitives", layoutPrimitives));
		items.push(explorer.keyed("application-patterns", applicationPatterns(explorer)));
		items.push(explorer.keyed("layout-stack", explorer.panel("stack-demo", [
			explorer.keyed("heading", explorer.heading("Stack + clipping")),
			explorer.keyed("copy", explorer.caption("Positioned children paint by z-index and inherit their parent's clip.")),
			explorer.keyed("stack", explorer.stackDemo())
		])));
	}

	static function applicationPatterns(explorer:UiExplorer):Column {
		var toolbarStyle = new LayoutStyle();
		toolbarStyle.width = LayoutAxis.grow();
		toolbarStyle.height = LayoutAxis.fixed(46.0);
		toolbarStyle.direction = LayoutDirection.LeftToRight;
		toolbarStyle.childAlignY = LayoutAlignmentY.Center;
		toolbarStyle.childDistribution = LayoutDistribution.SpaceBetween;
		toolbarStyle.padding = new Insets(4.0, 8.0, 4.0, 8.0);
		toolbarStyle.background = explorer.state.lightTheme
			? UiExplorer.color(0.90, 0.93, 0.98) : UiExplorer.color(0.08, 0.12, 0.19);

		var baselineStyle = new LayoutStyle();
		baselineStyle.width = LayoutAxis.grow();
		baselineStyle.height = LayoutAxis.fixed(42.0);
		baselineStyle.direction = LayoutDirection.LeftToRight;
		baselineStyle.childAlignY = LayoutAlignmentY.Baseline;
		baselineStyle.childGap = 10.0;

		var flowStyle = new LayoutStyle();
		flowStyle.width = LayoutAxis.grow(0.0, 520.0);
		flowStyle.height = LayoutAxis.fit();
		flowStyle.direction = LayoutDirection.LeftToRight;
		flowStyle.wrapMode = LayoutWrapMode.Wrap;
		flowStyle.rowGap = 8.0;
		flowStyle.columnGap = 8.0;

		var aspectStyle = new LayoutStyle();
		aspectStyle.width = LayoutAxis.grow(160.0, 420.0);
		aspectStyle.height = LayoutAxis.fit();
		aspectStyle.aspectRatio = 2.0;
		aspectStyle.padding = new Insets(8.0, 8.0, 8.0, 8.0);
		aspectStyle.background = explorer.state.lightTheme
			? UiExplorer.color(0.86, 0.90, 0.96) : UiExplorer.color(0.07, 0.10, 0.16);

		return explorer.panel("application-patterns", [
			explorer.keyed("heading", explorer.heading("Application shell patterns")),
			explorer.keyed("copy", explorer.caption(
				"Distribution, baseline alignment, wrapped flow, and bounded aspect-ratio content compose without spacer nodes.")),
			explorer.keyed("distributed-toolbar", new Row("distributed-toolbar", [
				explorer.keyed("back", explorer.button("Back", "layout-back", function() {})),
				explorer.keyed("toolbar-label", explorer.label("SpaceBetween toolbar")),
				explorer.keyed("save", explorer.button("Save", "layout-save", function() {}))
			], toolbarStyle)),
			explorer.keyed("baseline-row", new Row("baseline-row", [
				explorer.keyed("large", explorer.heading("Aa")),
				explorer.keyed("label", explorer.label("Shared baseline")),
				explorer.keyed("caption", explorer.caption("mixed text metrics")),
				explorer.keyed("control", explorer.button("Apply", "baseline-apply", function() {}))
			], baselineStyle)),
			explorer.keyed("wrapped-flow", new Row("wrapped-flow", [
				explorer.keyed("new", explorer.button("New file", "flow-new", function() {})),
				explorer.keyed("recent", explorer.button("Open recent", "flow-recent", function() {})),
				explorer.keyed("share", explorer.button("Share", "flow-share", function() {})),
				explorer.keyed("export", explorer.button("Export", "flow-export", function() {})),
				explorer.keyed("settings", explorer.button("Settings", "flow-settings", function() {}))
			], flowStyle)),
			explorer.keyed("aspect-preview", new Align("aspect-preview",
				explorer.caption("2:1 surface · min 160 · max 420"),
				LayoutAlignmentX.Center, LayoutAlignmentY.Center, aspectStyle))
		]);
	}
}
