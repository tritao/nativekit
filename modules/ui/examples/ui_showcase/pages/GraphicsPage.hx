package pages;

import UiExplorer;
import nativekit.ui.widgets.KeyedView;

/** The original retained graphics workload, presented as an Explorer page. */
class GraphicsPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Graphics Lab",
			"The original retained graphics demonstration remains part of this showcase.");
		items.push(explorer.keyed("graphics-card", explorer.panel("graphics-card", [
			explorer.keyed("heading", explorer.text("Paths · text · images · offscreen rendering",
				explorer.paletteText())),
			explorer.keyed("copy", explorer.text("Explore Bézier paths and stroke joins, multilingual shaping and caret hit testing, clipped image layers, retained display lists, and the depth-tested cube. The full graphics canvas keeps its existing renderer and deterministic visual tests.",
				explorer.paletteMuted())),
			explorer.keyed("launch", explorer.button("Open full Graphics Lab", "open-graphics-lab",
				explorer.onOpenGraphics)),
			explorer.keyed("hint", explorer.text("Press Escape in the Graphics Lab to return here.",
				explorer.paletteMuted()))
		])));
		items.push(explorer.keyed("graphics-pipeline", explorer.panel("graphics-pipeline", [
			explorer.keyed("heading", explorer.text("Graphics is one page in the explorer",
				explorer.paletteText())),
			explorer.keyed("copy", explorer.text("The catalog shell and framework demos use UiContext.submit → native layout → render. The original canvas lab remains available as the focused graphics workload.",
				explorer.paletteMuted()))
		])));
	}
}
