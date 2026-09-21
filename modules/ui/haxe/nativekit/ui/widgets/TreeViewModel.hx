package nativekit.ui.widgets;

import nativekit.ui.core.View;

/**
 * Application-owned hierarchy data source for TreeView.
 *
 * Collapsed branches are not traversed during index construction. The model
 * still needs a cheap root-key/default-expansion path for large root sets;
 * the current implementation performs that initial metadata scan lazily with
 * respect to child branches, not as a bulk range operation.
 */
interface TreeViewModel {
	function rootCount():Int;
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
