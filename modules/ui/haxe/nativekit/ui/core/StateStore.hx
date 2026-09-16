package nativekit.ui.core;

/** Haxe-owned per-widget state retained while views are rebuilt. */
class StateStore {
	final values:Map<Int, Dynamic>;
	final disposers:Map<Int, Void->Void>;
	final paths:Map<Int, String>;
	final managed:Map<Int, Bool>;
	final frameUsed:Map<Int, Bool>;
	var frameActive:Bool;
	public var revision(default, null):Int;

	public function new() {
		values = new Map();
		disposers = new Map();
		paths = new Map();
		managed = new Map();
		frameUsed = new Map();
		frameActive = false;
		revision = 0;
	}

	/** Records the scoped key path used to make a widget ID for diagnostics. */
	public function rememberPath(id:WidgetId, path:String):Void {
		if (id != null && path != null)
			paths.set(id.value, path);
	}

	public function initialize(id:WidgetId, initial:Dynamic):Void {
		if (id == null)
			throw "State requires a widget ID";
		if (!values.exists(id.value)) {
			values.set(id.value, initial);
		}
		markUsed(id);
	}

	/** Starts liveness tracking for resource-backed widget state. */
	public function beginFrame():Void {
		frameUsed.clear();
		frameActive = true;
	}

	/** Marks an initialized state as used by the current render tree. */
	public function touch(id:WidgetId):Void {
		if (id == null || !values.exists(id.value))
			throw "Widget state has not been initialized";
		markUsed(id);
	}

	/** Releases resource-backed state that was absent from the completed tree. */
	public function endFrame():Void {
		if (!frameActive)
			return;
		var stale:Array<Int> = [];
		for (id in managed.keys())
			if (!frameUsed.exists(id))
				stale.push(id);
		var failure:Dynamic = null;
		for (id in stale) {
			var disposer = disposers.get(id);
			try {
				if (disposer != null)
					disposer();
			} catch (error:Dynamic) {
				if (failure == null)
					failure = error;
			}
			disposers.remove(id);
			managed.remove(id);
			values.remove(id);
		}
		frameUsed.clear();
		frameActive = false;
		if (stale.length > 0)
			revision++;
		if (failure != null)
			throw failure;
	}

	@:allow(nativekit.ui.core.State)
	function getValue(id:WidgetId):Dynamic {
		if (id == null || !values.exists(id.value))
			throw 'Widget state has not been initialized for ${describe(id)}';
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

	public function describe(id:WidgetId):String {
		if (id == null)
			return "null";
		var path = paths.get(id.value);
		return path == null ? Std.string(id.value) : Std.string(id.value) + " (" + path + ")";
	}

	/** Registers one native-resource cleanup callback for persistent widget state. */
	public function onDispose(id:WidgetId, disposer:Void->Void):Void {
		if (id == null || disposer == null || !values.exists(id.value) || disposers.exists(id.value))
			throw "State disposal requires initialized state and one callback per widget ID";
		disposers.set(id.value, disposer);
	}

	/** Registers a resource cleanup callback and enables per-frame liveness cleanup. */
	public function onUnmount(id:WidgetId, disposer:Void->Void):Void {
		onDispose(id, disposer);
		managed.set(id.value, true);
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
		managed.clear();
		frameUsed.clear();
		frameActive = false;
		values.clear();
		paths.clear();
		revision++;
		if (failure != null)
			throw failure;
	}

	function markUsed(id:WidgetId):Void {
		if (frameActive)
			frameUsed.set(id.value, true);
	}
}
