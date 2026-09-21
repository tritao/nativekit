package nativekit.ui.core;

/** Typed accessors for arguments supplied by palettes, menus, or scripts. */
class CommandParameters {
	final values:Map<String, Dynamic>;

	public function new() {
		values = new Map();
	}

	public function has(name:String):Bool
		return name != null && values.exists(name);

	public function set(name:String, value:Dynamic):Void {
		if (name == null || name.length == 0)
			throw "Command parameter names require a stable name";
		values.set(name, value);
	}

	public function setString(name:String, value:String):Void
		set(name, value);

	public function setBool(name:String, value:Bool):Void
		set(name, value);

	public function setFloat(name:String, value:Float):Void
		set(name, value);

	public function setInt(name:String, value:Int):Void
		set(name, value);

	public function getString(name:String):Null<String> {
		var value = values.get(name);
		return value == null || !Std.isOfType(value, String) ? null : cast value;
	}

	public function getBool(name:String):Null<Bool> {
		var value = values.get(name);
		return value == null || !Std.isOfType(value, Bool) ? null : cast value;
	}

	public function getFloat(name:String):Null<Float> {
		var value = values.get(name);
		if (value == null || (!Std.isOfType(value, Float) && !Std.isOfType(value, Int)))
			return null;
		return cast value;
	}

	public function getInt(name:String):Null<Int> {
		var value = values.get(name);
		if (value == null || (!Std.isOfType(value, Int) && !Std.isOfType(value, Float)))
			return null;
		return Std.int(value);
	}

	public function get(name:String):Dynamic
		return values.get(name);

	public function copy():CommandParameters {
		var result = new CommandParameters();
		for (name in values.keys())
			result.set(name, values.get(name));
		return result;
	}
}
