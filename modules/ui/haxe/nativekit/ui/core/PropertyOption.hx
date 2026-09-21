package nativekit.ui.core;

/** Stable option metadata used by enum property editors. */
class PropertyOption {
	public final key:String;
	public final label:String;
	public var enabled:Bool;

	public function new(key:String, label:String, enabled:Bool = true) {
		if (key == null || key.length == 0)
			throw "Property options require a stable key";
		this.key = key;
		this.label = label == null ? key : label;
		this.enabled = enabled;
	}
}
