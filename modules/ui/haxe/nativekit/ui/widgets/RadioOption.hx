package nativekit.ui.widgets;

/** One keyed option in a RadioGroup. */
class RadioOption {
	public final key:String;
	public final label:String;
	public final value:String;
	public final enabled:Bool;

	public function new(key:String, label:String, value:String, enabled:Bool = true) {
		if (key == null || key.length == 0 || value == null || value.length == 0)
			throw "Radio options require stable keys and non-empty values";
		this.key = key;
		this.label = label == null ? "" : label;
		this.value = value;
		this.enabled = enabled;
	}
}
