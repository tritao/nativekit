package nativekit.ui.widgets;

import nativekit.ui.core.View;

/**
 * Application-owned data source for ListView.
 *
 * Keys must remain stable for an item while it is present in the model. The
 * revision is incremented by the model when count, keys, extents, or item
 * content change. `extentRevisionAt` must change whenever `extentAt` can
 * return a different value for that item.
 */
interface ListViewModel {
	function count():Int;
	function keyAt(index:Int):String;
	function extentAt(index:Int):Float;
	function extentRevisionAt(index:Int):Int;
	function buildItem(index:Int):View;
	function revision():Int;
}
