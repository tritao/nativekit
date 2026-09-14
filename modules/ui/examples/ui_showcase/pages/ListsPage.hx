package pages;

import UiExplorer;
import nativekit.ui.widgets.KeyedView;

/** Fixed-row virtualized scrolling and visible-range reporting. */
class ListsPage {
	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Scrolling & Data",
			"A fixed-row VirtualList with a real 10,000-item data set and explicit visible-range reporting.");
		var first = Std.int(explorer.listController.offsetY / UiExplorer.LIST_ROW_HEIGHT) + 1;
		var last = Std.int((explorer.listController.offsetY +
			Math.max(0.0, explorer.listController.viewportHeight)) / UiExplorer.LIST_ROW_HEIGHT) + 1;
		if (last > UiExplorer.LIST_COUNT)
			last = UiExplorer.LIST_COUNT;
		if (first > UiExplorer.LIST_COUNT)
			first = UiExplorer.LIST_COUNT;
		items.push(explorer.keyed("list-status", explorer.panel("list-status", [
			explorer.keyed("status", explorer.text('Rendering rows ${first}–${last} / ${UiExplorer.LIST_COUNT}',
				UiExplorer.color(0.35, 0.85, 0.69))),
			explorer.keyed("copy", explorer.text("Scroll inside the list. Only the viewport window and a small overscan are built as Haxe widgets.",
				explorer.paletteMuted())),
			explorer.keyed("jump", explorer.button("Jump to row 415", "jump-row", function() {
				explorer.listController.jumpTo(0.0, 414.0 * UiExplorer.LIST_ROW_HEIGHT);
			}))
		])));
		items.push(explorer.keyed("virtual-list", explorer.virtualList));
	}
}
