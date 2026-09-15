package pages;

import UiExplorer;
import components.DemoCard;
import components.DemoGrid;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.ProgressBar;
import nativekit.ui.widgets.Row;
import nativekit.ui.widgets.Toggle;

/** At-a-glance explanation of the NativeKit UI runtime and live controls. */
class OverviewPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Haxe UI, rendered by NativeKit",
			"A live explorer for composition, native layout, input and rendering.");
		var cards = DemoGrid.build("overview-cards", [
			explorer.keyed("platform-card", DemoCard.build("platform-card", "PLATFORM",
				explorer.platformLabel, "NativeKit owns the window, input and graphics surface.",
				explorer.panelStyle(),
				UiExplorer.color(0.40, 0.74, 0.92))),
			explorer.keyed("composition-card", DemoCard.build("composition-card", "COMPOSITION",
				"Haxe widgets", "Stable widget IDs keep focus and state across frames.",
				explorer.panelStyle(),
				UiExplorer.color(0.40, 0.74, 0.92))),
			explorer.keyed("layout-card", DemoCard.build("layout-card", "LAYOUT", "Native engine",
				"Submit the tree, resolve bounds, then render the result.", explorer.panelStyle(),
				UiExplorer.color(0.40, 0.74, 0.92)))
		], explorer.rowStyle(0.0));
		items.push(explorer.keyed("overview-cards", cards));
		items.push(explorer.keyed("overview-quick-start", explorer.panel("quick-start", [
			explorer.keyed("quick-title", explorer.heading("Try the framework")),
			explorer.keyed("quick-copy", explorer.caption("Switch pages from the catalog. Edit the multilingual text field, tab through controls, scroll the virtual list, or open the live inspector.")),
			explorer.keyed("quick-controls", new Row("quick-controls", [
				explorer.keyed("toggle", new Toggle("overview-toggle", "Enable preview", explorer.state.controls.enabled,
					function(value) { explorer.state.controls.enabled = value; })),
				explorer.keyed("progress", new ProgressBar("overview-progress", explorer.state.controls.progress,
					0.0, 1.0, "Preview progress"))
			], explorer.rowStyle(18.0)))
		])));
		items.push(explorer.keyed("overview-pipeline", explorer.panel("pipeline", [
			explorer.keyed("pipeline-title", explorer.heading("One application, two runtimes")),
			explorer.keyed("pipeline-copy", explorer.caption("NativeKit provides platform, IME and graphics services. Haxe owns the UI tree, state and semantics. The same showcase runs on desktop and WebAssembly."))
		])));
		items.push(explorer.keyed("overview-motion", explorer.panel("motion", [
			explorer.keyed("motion-title", explorer.heading("Haxe-owned motion")),
			explorer.keyed("motion-copy", explorer.caption('Tween ${Std.int(explorer.state.gestures.tweenValue * 100)}%  ·  Spring ${Std.int(explorer.state.gestures.springValue * 100)}%')),
			explorer.keyed("motion-actions", new Row("motion-actions", [
				explorer.keyed("play-tween", explorer.button("Play tween", "play-tween", function() {
					explorer.tweenController.play(0.0, 1.0, 0.8);
				})),
				explorer.keyed("play-spring", explorer.button("Spring bounce", "play-spring", function() {
					explorer.springController.setTarget(explorer.state.gestures.springValue < 0.5 ? 1.0 : 0.18);
				}))
			], explorer.rowStyle(10.0)))
		])));
	}
}
