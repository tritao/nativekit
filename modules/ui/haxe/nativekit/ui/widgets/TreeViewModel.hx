package nativekit.ui.widgets;

import nativekit.ui.core.View;

/**
 * Application-owned hierarchy data source for TreeView.
 *
 * Collapsed branches are not traversed during index construction. Root keys
 * and their default expansion state are supplied in one bulk range call so a
 * large root set does not cross the model boundary once per root.
 */
interface TreeViewModel {
	function rootCount():Int;
	function rootRange(start:Int, count:Int):Array<TreeRootMetadata>;
	/** @deprecated Implement rootRange; retained for source migration only. */
	function rootKeyAt(index:Int):String;
	function childCount(parentKey:String):Int;
	function childKeyAt(parentKey:String, index:Int):String;
	function initiallyExpanded(key:String):Bool;
	/** Estimate used for branches that have not entered the materialization window. */
	function estimatedExtent():Float;
	/** Allows TreeView to skip all extent callbacks for uniform rows. */
	function extentIsUniform():Bool;
	function extentAt(key:String):Float;
	function buildItem(key:String):View;
	function revision():Int;
}
