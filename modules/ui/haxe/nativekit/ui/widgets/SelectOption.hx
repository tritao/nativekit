package nativekit.ui.widgets;

/** Immutable typed option used by Select. */
class SelectOption<T> {
	public final key:String;
	public final label:String;
	public final value:T;
	public final enabled:Bool;

	public function new(key:String, label:String, value:T, enabled:Bool = true) {
		if (key == null || key.length == 0)
			throw "Select options require stable non-empty keys";
		this.key = key;
		this.label = label == null ? "" : label;
		this.value = value;
		this.enabled = enabled;
	}
}
