package nativekit.ui.widgets;

import nativekit.ui.core.View;

/**
 * Application-owned data source for ListView.
 *
 * Keys must remain stable for an item while it is present in the model. The
 * revision is incremented by the model when count, keys, extents, or item
 * content change. `extentRevisionAt` must change whenever `extentAt` can
 * return a different value for that item. `estimatedExtent` is used for
 * unmeasured items; declare `extentIsUniform` when it is exact for every item
 * so the list can skip all extent callbacks. `totalExtent` may return an
 * exact content height when the model can provide one cheaply; return null
 * when only the sparse estimate is available. Variable-height models should
 * expect anchor corrections as rows are seen.
 */
interface ListViewModel {
	function count():Int;
	function keyAt(index:Int):String;
	function estimatedExtent():Float;
	function extentIsUniform():Bool;
	function extentAt(index:Int):Float;
	function extentRevisionAt(index:Int):Int;
	function totalExtent():Null<Float>;
	function buildItem(index:Int):View;
	function revision():Int;
}
