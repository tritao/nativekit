package nativekit.ui.core;

import haxe.io.Bytes;

/** Length-prefixed key path with deterministic UTF-8 hashing. */
class KeyScope {
	final path:String;

	public function new(path:String = "") {
		this.path = path;
	}

	public function child(key:Key):KeyScope {
		if (key == null)
			throw "A key scope requires a key";
		return new KeyScope(path + key.value.length + ":" + key.value + "|");
	}

	public function widgetId(localKey:String):WidgetId {
		if (localKey == null || localKey.length == 0)
			throw "Local widget keys must not be empty";
		var bytes = Bytes.ofString(path + localKey.length + ":" + localKey);
		var hash = -2128831035;
		for (index in 0...bytes.length)
			hash = (hash ^ bytes.get(index)) * 16777619;
		var value = hash & 0x7fffffff;
		return new WidgetId(value == 0 ? 1 : value);
	}

	public inline function pathValue():String
		return path;
}
