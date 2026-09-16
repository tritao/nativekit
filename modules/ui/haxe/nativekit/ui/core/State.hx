package nativekit.ui.core;

/** Stable handle to one value in a UiContext's persistent state store. */
class State<T> {
	final store:StateStore;
	public final id:WidgetId;

	@:allow(nativekit.ui.core.BuildContext)
	private function new(store:StateStore, id:WidgetId) {
		this.store = store;
		this.id = id;
	}

	public var value(get, never):T;
	inline function get_value():T
		// StateStore intentionally holds heterogeneous widget values; this is the
		// single typed boundary back into a State<T> handle.
		return cast store.getValue(id);

	public function update(value:T):T {
		store.setValue(id, value);
		return value;
	}
}
