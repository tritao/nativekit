package nativekit.ui.style;

/** One ordered typed selector/declaration rule. */
class StyleRule {
	public final selector:StyleSelector;
	public final declarations:Array<StyleValue>;
	public final order:Int;
	public final stylesheet:String;
	public final condition:Null<EnvironmentCondition>;

	public function new(stylesheet:String, selector:StyleSelector,
			declarations:Array<StyleValue>, order:Int, ?condition:EnvironmentCondition) {
		if (selector == null || declarations == null || declarations.length == 0)
			throw "Style rules require a selector and declarations";
		this.stylesheet = stylesheet == null ? "" : stylesheet;
		this.selector = selector;
		this.declarations = declarations.copy();
		this.order = order;
		this.condition = condition;
	}

	public function matches(target:StyleTarget, ?environment:StyleEnvironment):Bool
		return selector.matches(target) && (condition == null || condition.matches(environment));
}
