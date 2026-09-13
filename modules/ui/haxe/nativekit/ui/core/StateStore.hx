package nativekit.ui.core;

/** Haxe-owned per-widget state retained while views are rebuilt. */
class StateStore {
	final values:Map<Int, Dynamic>;
	public var revision(default, null):Int;

	public function new() {
		values = new Map();
		revision = 0;
	}

	public function initialize(id:WidgetId, initial:Dynamic):Void {
		if (id == null)
			throw "State requires a widget ID";
		if (!values.exists(id.value))
			values.set(id.value, initial);
	}

	@:allow(nativekit.ui.core.State)
	function getValue(id:WidgetId):Dynamic {
		if (id == null || !values.exists(id.value))
			throw "Widget state has not been initialized";
		return values.get(id.value);
	}

	@:allow(nativekit.ui.core.State)
	function setValue(id:WidgetId, value:Dynamic):Void {
		if (id == null)
			throw "State requires a widget ID";
		values.set(id.value, value);
		revision++;
	}

	public function contains(id:WidgetId):Bool
		return id != null && values.exists(id.value);
}
