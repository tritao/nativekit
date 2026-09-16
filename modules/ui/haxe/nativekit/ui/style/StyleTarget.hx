package nativekit.ui.style;

/** Style-facing identity and interaction data for one render node. */
class StyleTarget {
	public final widgetType:String;
	public final key:Null<String>;
	public final id:Null<String>;
	public final classes:Array<String>;
	public final tags:Array<String>;
	public final states:Int;
	/** Stable value fingerprint for selector inputs other than pseudo-state. */
	public final selectorFingerprint:String;

	public function new(widgetType:String, ?key:String, ?id:String,
			?classes:Array<String>, ?tags:Array<String>, states:Int = 0) {
		if (widgetType == null || widgetType.length == 0)
			throw "Style targets require a widget type";
		this.widgetType = widgetType;
		this.key = key;
		this.id = id;
		this.classes = classes == null ? [] : classes.copy();
		this.tags = tags == null ? [] : tags.copy();
		this.states = states;
		selectorFingerprint = stringKey(widgetType) + "|key=" + stringKey(key) +
			"|id=" + stringKey(id) + "|classes=" + valuesKey(this.classes) +
			"|tags=" + valuesKey(this.tags);
	}

	static function valuesKey(values:Array<String>):String {
		var result = values.length + ":";
		for (value in values)
			result += stringKey(value) + ";";
		return result;
	}

	static function stringKey(value:Null<String>):String
		return value == null ? "-1:" : value.length + ":" + value;

	public function hasState(state:StyleState):Bool
		return StyleStateUtil.contains(states, state);

	public function hasClass(name:String):Bool
		return contains(classes, name);

	public function hasTag(name:String):Bool
		return contains(tags, name);

	static function contains(values:Array<String>, value:String):Bool {
		if (value == null)
			return false;
		for (present in values)
			if (present == value)
				return true;
		return false;
	}
}
