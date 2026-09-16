package nativekit.ui.core;

/** Persistent generic interaction flags, separate from widget-owned values. */
class InteractionStateStore {
	final values:Map<Int, Int>;
	public var revision(default, null):Int;

	public function new() {
		values = new Map();
		revision = 0;
	}

	public function get(id:WidgetId):Int
		return id == null || !values.exists(id.value) ? 0 : values.get(id.value);

	public function set(id:WidgetId, value:Int):Void {
		if (id == null)
			throw "Interaction state requires a widget ID";
		var previous = get(id);
		if (previous == value)
			return;
		values.set(id.value, value);
		revision++;
	}

	public function dispose():Void {
		values.clear();
		revision++;
	}
}
