package nativekit.ui.core;

/** Stable handle to one value in a UiContext's persistent state store. */
class State<T> {
	final store:StateStore;
	public final id:WidgetId;

	@:allow(nativekit.ui.core.BuildContext)
	private function new(store:Dynamic, id:Dynamic) {
		this.store = cast store;
		this.id = cast id;
	}

	public var value(get, never):Dynamic;
	inline function get_value():Dynamic
		return store.getValue(id);

	public function update(value:Dynamic):Dynamic {
		store.setValue(id, value);
		return value;
	}
}
