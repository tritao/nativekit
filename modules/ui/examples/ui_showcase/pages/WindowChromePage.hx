package pages;

import LayoutAxis;
import LayoutDirection;
import LayoutStyle;
import UiExplorer;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.Row;

/** Explains and launches the interactive multi-window custom chrome demo. */
class WindowChromePage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Custom window chrome",
			"Compose a borderless native window from ordinary Haxe UI views and explicit hit-test regions.");

		items.push(explorer.keyed("window-chrome-launch", explorer.panel("window-chrome-launch-panel", [
			explorer.keyed("heading", explorer.heading("Open a real second window")),
			explorer.keyed("description", explorer.caption(
			"The demo window is created by NativeKit, rendered by the UI framework, and routed through the same event pump as this explorer.")),
			explorer.keyed("actions", new Row("window-chrome-actions", [
				explorer.keyed("open", explorer.button("Open Window Chrome Demo", "open-window-chrome",
					function() { explorer.openWindowChromeDemo(); })),
				explorer.keyed("status", explorer.caption(explorer.windowChromeStatus()))
		], explorer.rowStyle(14.0)))
		])));

		var cardsStyle = new LayoutStyle();
		cardsStyle.width = LayoutAxis.grow();
		cardsStyle.height = LayoutAxis.fit();
		cardsStyle.direction = LayoutDirection.LeftToRight;
		cardsStyle.childGap = 14.0;
		items.push(explorer.keyed("window-chrome-regions", new Row("window-chrome-regions", [
			explorer.keyed("drag", explorer.panel("drag-region", [
				explorer.keyed("title", explorer.heading("Drag region")),
				explorer.keyed("copy", explorer.caption(
					"A WindowChrome view marked Drag lets the user move the native window."))
			])),
			explorer.keyed("client", explorer.panel("client-region", [
				explorer.keyed("title", explorer.heading("Client holes")),
				explorer.keyed("copy", explorer.caption(
					"Buttons and other controls can be marked Client so they remain interactive inside a drag region."))
			])),
			explorer.keyed("resize", explorer.panel("resize-regions", [
				explorer.keyed("title", explorer.heading("Resize regions")),
				explorer.keyed("copy", explorer.caption(
					"Eight edge and corner regions delegate native resizing while the window stays borderless."))
			]))
		], cardsStyle)));

		items.push(explorer.keyed("window-chrome-notes", explorer.panel("window-chrome-notes-panel", [
			explorer.keyed("title", explorer.heading("What to try")),
			explorer.keyed("copy", explorer.caption(
			"In the new window, drag the title bar, click the client button, and resize from every edge and corner. Close it independently to return here.")),
			explorer.keyed("api", explorer.caption(
			"The declarative API is WindowChrome(view, kind); UiContext projects its resolved bounds to NativeKit's window decoration regions."))
		])));
	}
}
