package nativekit.ui.widgets;

/** UTF-8 helpers using the code-point offsets used by NativeKit text APIs. */
class Utf8Text {
	public static function length(text:String):Int
		return TextOffsetMap.countCodepoints(text);

	/** Builds a reusable map for the supplied document or paragraph. */
	public static function offsets(text:String):TextOffsetMap
		return new TextOffsetMap(text);

	public static function slice(text:String, start:Int, end:Int):String
		return new TextOffsetMap(text).sliceCodepoints(start, end);

	public static function replace(text:String, start:Int, end:Int,
			replacement:Null<String>):String
		return new TextOffsetMap(text).replaceCodepoints(start, end, replacement);

}
