package nativekit.ui.style;

/** Ordered collection of typed style rules. */
class StyleSheet {
	public final name:String;
	public final rules(default, null):Array<StyleRule>;

	public function new(?name:String) {
		this.name = name == null || name.length == 0 ? "StyleSheet" : name;
		rules = [];
	}

	public function rule(selector:StyleSelector, declarations:Array<StyleValue>):StyleRule {
		var result = new StyleRule(name, selector, declarations, rules.length);
		rules.push(result);
		return result;
	}

	public function clear():Void
		rules.resize(0);

	public function isEmpty():Bool
		return rules.length == 0;
}
