package nativekit.ui.style;

/** Style-facing identity and interaction data for one render node. */
class StyleTarget {
	public final widgetType:String;
	public final key:Null<String>;
	public final id:Null<String>;
	public final classes:Array<String>;
	public final tags:Array<String>;
	public final states:Int;

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
	}

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
