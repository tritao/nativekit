package pages;

import LayoutAlignment;
import LayoutAxis;
import LayoutStyle;
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
				explorer.keyed("heading", explorer.text("Row + Column", explorer.paletteText())),
				explorer.keyed("copy", explorer.text("Fixed gaps and grow sizing", explorer.paletteMuted())),
				explorer.keyed("row", new Row("sample-row", [
					explorer.keyed("one", explorer.colorTile("One", UiExplorer.color(0.24, 0.48, 0.77))),
					explorer.keyed("two", explorer.colorTile("Two", UiExplorer.color(0.39, 0.34, 0.72))),
					explorer.keyed("three", explorer.colorTile("Three", UiExplorer.color(0.20, 0.58, 0.52)))
				], explorer.rowStyle(8.0)))
			])),
			explorer.keyed("padding-align", explorer.panel("padding-align", [
				explorer.keyed("heading", explorer.text("Padding + Align", explorer.paletteText())),
				explorer.keyed("aligned", new Align("centered-content",
					explorer.colorTile("Centered in a padded box", UiExplorer.color(0.30, 0.40, 0.59)),
					LayoutAlignment.Center, LayoutAlignment.Center, explorer.fixedBoxStyle(270.0, 90.0)))
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
		items.push(explorer.keyed("layout-stack", explorer.panel("stack-demo", [
			explorer.keyed("heading", explorer.text("Stack + clipping", explorer.paletteText())),
			explorer.keyed("copy", explorer.text("Positioned children paint by z-index and inherit their parent's clip.",
				explorer.paletteMuted())),
			explorer.keyed("stack", explorer.stackDemo())
		])));
	}
}
