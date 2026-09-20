package nativekit.ui.widgets;

/** Stable metadata describing one fixed-width table column. */
class TableColumn {
	public final key:String;
	public final label:String;
	public final width:Float;

	public function new(key:String, label:String, width:Float) {
		if (key == null || key.length == 0 || width <= 0.0 || !finite(width))
			throw "Table columns require a stable key and finite positive width";
		this.key = key;
		this.label = label == null ? "" : label;
		this.width = width;
	}

	static inline function finite(value:Float):Bool
		return value == value && value - value == 0.0;
}
