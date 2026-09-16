package nativekit.ui.style;

/** One ordered typed selector/declaration rule. */
class StyleRule {
	public final selector:StyleSelector;
	public final declarations:Array<StyleValue>;
	public final order:Int;
	public final stylesheet:String;

	public function new(stylesheet:String, selector:StyleSelector,
			declarations:Array<StyleValue>, order:Int) {
		if (selector == null || declarations == null || declarations.length == 0)
			throw "Style rules require a selector and declarations";
		this.stylesheet = stylesheet == null ? "" : stylesheet;
		this.selector = selector;
		this.declarations = declarations.copy();
		this.order = order;
	}

	public function matches(target:StyleTarget):Bool
		return selector.matches(target);
}
