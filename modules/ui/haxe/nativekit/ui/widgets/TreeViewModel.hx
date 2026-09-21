package nativekit.ui.widgets;

import nativekit.ui.core.View;

/** Application-owned hierarchy data source for TreeView. */
interface TreeViewModel {
	function rootCount():Int;
	function rootKeyAt(index:Int):String;
	function childCount(parentKey:String):Int;
	function childKeyAt(parentKey:String, index:Int):String;
	function initiallyExpanded(key:String):Bool;
	function estimatedExtent():Float;
	function extentIsUniform():Bool;
	function extentAt(key:String):Float;
	function buildItem(key:String):View;
	function revision():Int;
}
