package pages;

import UiExplorer;
import components.StatusBadge;
import Insets;
import LayoutAxis;
import LayoutStyle;
import nativekit.ui.core.View;
import nativekit.ui.widgets.ListView;
import nativekit.ui.widgets.ListViewModel;
import nativekit.ui.widgets.Text;
import nativekit.ui.widgets.KeyedView;

/** Model-backed virtualized scrolling and visible-range reporting. */
class ListsPage {
	public static function createListView(explorer:UiExplorer):ListView {
		var listStyle = new LayoutStyle();
		listStyle.width = LayoutAxis.grow();
		listStyle.height = LayoutAxis.fixed(350.0);
		listStyle.clipVertical = true;
		return new ListView("ten-thousand-rows", new ShowcaseListModel(explorer),
			listStyle, explorer.state.listController, 350.0);
	}

	public static function build(explorer:UiExplorer, items:Array<KeyedView>):Void {
		explorer.pageHeading(items, "Scrolling & Data",
			"A model-backed ListView with a real 10,000-item data set, stable keys, semantics, and explicit visible-range reporting.");
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
			explorer.keyed("copy", explorer.caption("Scroll inside the list. ListView materializes only the viewport window and a small overscan as Haxe widgets.")),
			explorer.keyed("jump", explorer.button("Jump to row 415", "jump-row", function() {
				explorer.listView.scrollTo(414);
			}))
		])));
		items.push(explorer.keyed("list-view", explorer.listView));
	}
}

private class ShowcaseListModel implements ListViewModel {
	final explorer:UiExplorer;

	public function new(explorer:UiExplorer) {
		this.explorer = explorer;
	}

	public function count():Int
		return UiExplorer.LIST_COUNT;

	public function keyAt(index:Int):String
		return 'row-$index';

	public function extentAt(index:Int):Float
		return UiExplorer.LIST_ROW_HEIGHT;

	public function extentRevisionAt(index:Int):Int
		return 0;

	public function totalExtent():Null<Float>
		return UiExplorer.LIST_COUNT * UiExplorer.LIST_ROW_HEIGHT;

	public function buildItem(index:Int):View {
		var rowStyle = new LayoutStyle();
		rowStyle.width = LayoutAxis.grow();
		rowStyle.height = LayoutAxis.fixed(UiExplorer.LIST_ROW_HEIGHT);
		rowStyle.padding = new Insets(8.0, 7.0, 8.0, 7.0);
		rowStyle.background = explorer.state.lightTheme
			? (index % 2 == 0 ? UiExplorer.color(0.98, 0.99, 1.0)
				: UiExplorer.color(0.91, 0.94, 0.98))
			: (index % 2 == 0 ? UiExplorer.color(0.11, 0.14, 0.20)
				: UiExplorer.color(0.13, 0.16, 0.23));
		return new Text('ROW ${index + 1}  ·  virtual item', rowStyle);
	}

	public function revision():Int
		return explorer.state.lightTheme ? 1 : 2;
}
