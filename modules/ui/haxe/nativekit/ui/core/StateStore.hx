package nativekit.ui.core;

/** Haxe-owned per-widget state retained while views are rebuilt. */
class StateStore {
	final values:Map<Int, Dynamic>;
	final disposers:Map<Int, Void->Void>;
	public var revision(default, null):Int;

	public function new() {
		values = new Map();
		disposers = new Map();
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

	/** Registers one native-resource cleanup callback for persistent widget state. */
	public function onDispose(id:WidgetId, disposer:Void->Void):Void {
		if (id == null || disposer == null || !values.exists(id.value) || disposers.exists(id.value))
			throw "State disposal requires initialized state and one callback per widget ID";
		disposers.set(id.value, disposer);
	}

	/** Releases registered widget resources and clears the store. */
	public function dispose():Void {
		var failure:Dynamic = null;
		for (id in disposers.keys()) {
			var disposer = disposers.get(id);
			try {
				disposer();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
		}
		disposers.clear();
		values.clear();
		revision++;
		if (failure != null)
			throw failure;
	}
}
