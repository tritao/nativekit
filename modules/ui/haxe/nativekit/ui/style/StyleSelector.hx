package nativekit.ui.style;

/** Typed selector; deliberately does not parse or emit CSS syntax. */
class StyleSelector {
	public final widgetType:Null<String>;
	public final keyValue:Null<String>;
	public final idValue:Null<String>;
	public final classNames:Array<String>;
	public final tagNames:Array<String>;
	public final stateMask:Int;

	function new(?widgetType:String, ?keyValue:String, ?idValue:String,
			?classNames:Array<String>, ?tagNames:Array<String>, stateMask:Int = 0) {
		this.widgetType = widgetType;
		this.keyValue = keyValue;
		this.idValue = idValue;
		this.classNames = classNames == null ? [] : classNames;
		this.tagNames = tagNames == null ? [] : tagNames;
		this.stateMask = stateMask;
	}

	public static function any():StyleSelector
		return new StyleSelector();

	public static function widget(type:String):StyleSelector {
		if (type == null || type.length == 0)
			throw "Widget selectors require a type";
		return new StyleSelector(type);
	}

	public static function key(value:String):StyleSelector {
		if (value == null || value.length == 0)
			throw "Key selectors require a value";
		return new StyleSelector(null, value);
	}

	public static function id(value:String):StyleSelector {
		if (value == null || value.length == 0)
			throw "ID selectors require a value";
		return new StyleSelector(null, null, value);
	}

	/** Adds a class constraint and returns this selector for fluent composition. */
	public function className(value:String):StyleSelector {
		if (value == null || value.length == 0)
			throw "Class selectors require a value";
		classNames.push(value);
		return this;
	}

	/** Adds a semantic/tag constraint and returns this selector for fluent composition. */
	public function tag(value:String):StyleSelector {
		if (value == null || value.length == 0)
			throw "Tag selectors require a value";
		tagNames.push(value);
		return this;
	}

	/** Adds a pseudo-state constraint and returns this selector for fluent composition. */
	public function state(value:StyleState):StyleSelector {
		stateMask |= value;
		return this;
	}

	public function hasState():Bool
		return stateMask != 0;

	public function matches(target:StyleTarget):Bool {
		if (target == null)
			return false;
		if (widgetType != null && widgetType != target.widgetType)
			return false;
		if (keyValue != null && keyValue != target.key)
			return false;
		if (idValue != null && idValue != target.id)
			return false;
		for (name in classNames)
			if (!target.hasClass(name))
				return false;
		for (name in tagNames)
			if (!target.hasTag(name))
				return false;
		return (target.states & stateMask) == stateMask;
	}

	/** Deterministic cascade specificity, used only between matching rules in one layer. */
	public function specificity():Int
		return (idValue == null ? 0 : 100) +
			(classNames.length + tagNames.length + StyleStateUtil.count(stateMask)) * 10 +
			(widgetType == null ? 0 : 1);

	public function describe():String {
		var result = widgetType == null ? "*" : widgetType;
		if (idValue != null)
			result += "#" + idValue;
		if (keyValue != null)
			result += "@" + keyValue;
		for (name in classNames)
			result += "." + name;
		for (name in tagNames)
			result += "[" + name + "]";
		var states:Array<String> = [];
		if (StyleStateUtil.contains(stateMask, StyleState.Hovered)) states.push("hovered");
		if (StyleStateUtil.contains(stateMask, StyleState.Pressed)) states.push("pressed");
		if (StyleStateUtil.contains(stateMask, StyleState.Focused)) states.push("focused");
		if (StyleStateUtil.contains(stateMask, StyleState.Disabled)) states.push("disabled");
		if (StyleStateUtil.contains(stateMask, StyleState.Selected)) states.push("selected");
		if (StyleStateUtil.contains(stateMask, StyleState.Checked)) states.push("checked");
		for (state in states)
			result += ":" + state;
		return result;
	}
}
