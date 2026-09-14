package pages;

import UiExplorer;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Toggle;

/** At-a-glance explanation of the NativeKit UI runtime and live controls. */
class OverviewPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Haxe UI, rendered by NativeKit",
			"A live explorer for composition, native layout, input and rendering.");
		var cards = new Row("overview-cards", [
			explorer.keyed("platform-card", explorer.card("platform-card", "PLATFORM",
				explorer.platformLabel, "NativeKit owns the window, input and graphics surface.")),
			explorer.keyed("composition-card", explorer.card("composition-card", "COMPOSITION",
				"Haxe widgets", "Stable widget IDs keep focus and state across frames.")),
			explorer.keyed("layout-card", explorer.card("layout-card", "LAYOUT", "Native engine",
				"Submit the tree, resolve bounds, then render the result."))
		], explorer.rowStyle(0.0));
		items.push(explorer.keyed("overview-cards", cards));
		items.push(explorer.keyed("overview-quick-start", explorer.panel("quick-start", [
			explorer.keyed("quick-title", explorer.text("Try the framework", explorer.paletteText())),
			explorer.keyed("quick-copy", explorer.text("Switch pages from the catalog. Edit the multilingual text field, tab through controls, scroll the virtual list, or open the live inspector.", explorer.paletteMuted())),
			explorer.keyed("quick-controls", new Row("quick-controls", [
				explorer.keyed("toggle", new Toggle("overview-toggle", "Enable preview", explorer.enabled,
					function(value) { explorer.enabled = value; })),
				explorer.keyed("progress", new ProgressBar("overview-progress", explorer.progress,
					0.0, 1.0, "Preview progress"))
			], explorer.rowStyle(18.0)))
		])));
		items.push(explorer.keyed("overview-pipeline", explorer.panel("pipeline", [
			explorer.keyed("pipeline-title", explorer.text("One application, two runtimes",
				explorer.paletteText())),
			explorer.keyed("pipeline-copy", explorer.text("NativeKit provides platform, IME and graphics services. Haxe owns the UI tree, state and semantics. The same showcase runs on desktop and WebAssembly.",
				explorer.paletteMuted()))
		])));
		items.push(explorer.keyed("overview-motion", explorer.panel("motion", [
			explorer.keyed("motion-title", explorer.text("Haxe-owned motion", explorer.paletteText())),
			explorer.keyed("motion-copy", explorer.text('Tween ${Std.int(explorer.tweenValue * 100)}%  ·  Spring ${Std.int(explorer.springValue * 100)}%',
				explorer.paletteMuted())),
			explorer.keyed("motion-actions", new Row("motion-actions", [
				explorer.keyed("play-tween", explorer.button("Play tween", "play-tween", function() {
					explorer.tweenController.play(0.0, 1.0, 0.8);
				})),
				explorer.keyed("play-spring", explorer.button("Spring bounce", "play-spring", function() {
					explorer.springController.setTarget(explorer.springValue < 0.5 ? 1.0 : 0.18);
				}))
			], explorer.rowStyle(10.0)))
		])));
	}
}
