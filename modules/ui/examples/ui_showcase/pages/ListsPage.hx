package pages;

import UiExplorer;
import components.StatusBadge;
import Insets;
import LayoutAxis;
import LayoutStyle;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.KeyedView;
import nativekit.ui.widgets.VirtualList;

/** Fixed-row virtualized scrolling and visible-range reporting. */
class ListsPage {
	public static function createVirtualList(explorer:UiExplorer):VirtualList {
		var listStyle = new LayoutStyle();
		listStyle.width = LayoutAxis.grow();
		listStyle.height = LayoutAxis.fixed(350.0);
		listStyle.clipVertical = true;
		return new VirtualList("ten-thousand-rows", UiExplorer.LIST_COUNT,
			UiExplorer.LIST_ROW_HEIGHT, function(index) {
				var rowStyle = new LayoutStyle();
				rowStyle.width = LayoutAxis.grow();
				rowStyle.height = LayoutAxis.fixed(UiExplorer.LIST_ROW_HEIGHT);
				rowStyle.padding = new Insets(8.0, 7.0, 8.0, 7.0);
				rowStyle.background = explorer.state.lightTheme
					? (index % 2 == 0 ? UiExplorer.color(0.98, 0.99, 1.0)
						: UiExplorer.color(0.91, 0.94, 0.98))
					: (index % 2 == 0 ? UiExplorer.color(0.11, 0.14, 0.20)
						: UiExplorer.color(0.13, 0.16, 0.23));
				return new Text('ROW ${index + 1}  ·  virtual item', rowStyle,
					explorer.paletteText());
			}, listStyle, null, explorer.state.listController, 350.0);
	}

	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Scrolling & Data",
			"A fixed-row VirtualList with a real 10,000-item data set and explicit visible-range reporting.");
		var first = Std.int(explorer.state.listController.offsetY / UiExplorer.LIST_ROW_HEIGHT) + 1;
		var last = Std.int((explorer.state.listController.offsetY +
			Math.max(0.0, explorer.state.listController.viewportHeight)) / UiExplorer.LIST_ROW_HEIGHT) + 1;
		if (last > UiExplorer.LIST_COUNT)
			last = UiExplorer.LIST_COUNT;
		if (first > UiExplorer.LIST_COUNT)
			first = UiExplorer.LIST_COUNT;
		items.push(explorer.keyed("list-status", explorer.panel("list-status", [
			StatusBadge.build("status", 'Rendering rows ${first}–${last} / ${UiExplorer.LIST_COUNT}',
				UiExplorer.color(0.35, 0.85, 0.69)),
			explorer.keyed("copy", explorer.text("Scroll inside the list. Only the viewport window and a small overscan are built as Haxe widgets.",
				explorer.paletteMuted())),
			explorer.keyed("jump", explorer.button("Jump to row 415", "jump-row", function() {
				explorer.state.listController.jumpTo(0.0, 414.0 * UiExplorer.LIST_ROW_HEIGHT);
			}))
		])));
		items.push(explorer.keyed("virtual-list", explorer.virtualList));
	}
}
