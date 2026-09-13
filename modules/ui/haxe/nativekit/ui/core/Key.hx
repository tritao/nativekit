package nativekit.ui.core;

/** Caller-chosen stable key that scopes widget identity. */
class Key {
	public final value:String;

	public function new(value:String) {
		if (value == null || value.length == 0)
			throw "Widget keys must not be empty";
		this.value = value;
	}
}
