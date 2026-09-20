package nativekit.ui.widgets;

/** Stable metadata describing one resizable table column. */
class TableColumn {
	public final key:String;
	public final label:String;
	public var width(default, null):Float;

	public function new(key:String, label:String, width:Float) {
		if (key == null || key.length == 0 || width <= 0.0 || !finite(width))
			throw "Table columns require a stable key and finite positive width";
		this.key = key;
		this.label = label == null ? "" : label;
		this.width = width;
	}

	/** Updates the logical width and reports whether it changed. */
	public function resize(next:Float):Bool {
		if (next <= 0.0 || !finite(next))
			throw "Table column width must be finite and positive";
		if (next == width)
			return false;
		width = next;
		return true;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
