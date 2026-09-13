package nativekit.ui.core;

/** Frame-local identity scopes backed by a persistent UiContext state store. */
class BuildContext {
	public final stateStore:StateStore;
	final claimed:Map<Int, String>;
	var scope:KeyScope;

	public function new(stateStore:StateStore) {
		if (stateStore == null)
			throw "Build context requires a state store";
		this.stateStore = stateStore;
		claimed = new Map();
		scope = new KeyScope();
	}

	public function beginFrame():Void {
		claimed.clear();
		scope = new KeyScope();
	}

	public function id(localKey:String):WidgetId {
		var id = scope.widgetId(localKey);
		if (claimed.exists(id.value))
			throw 'Duplicate widget ID ${id.value}; use distinct keys for sibling views';
		claimed.set(id.value, scope.pathValue() + localKey);
		return id;
	}

	public function withScope<T>(key:Key, build:Void->T):T {
		if (key == null || build == null)
			throw "A scoped build requires a key and callback";
		var previous = scope;
		scope = scope.child(key);
		try {
			var result = build();
			scope = previous;
			return result;
		} catch (error:Dynamic) {
			scope = previous;
			throw error;
		}
	}

	public function state<T>(id:WidgetId, initial:T):State<T> {
		stateStore.initialize(id, initial);
		var value:State<Dynamic> = new State<Dynamic>(stateStore, id);
		return cast value;
	}
}
