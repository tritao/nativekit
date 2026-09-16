package nativekit.ui.style;

/** Optional provenance attached to one computed property. */
class StyleSource {
	public final stylesheet:String;
	public final selector:String;
	public final rule:Int;
	public final layer:String;

	public function new(stylesheet:String, selector:String, rule:Int, layer:String) {
		this.stylesheet = stylesheet == null ? "" : stylesheet;
		this.selector = selector == null ? "" : selector;
		this.rule = rule;
		this.layer = layer == null ? "" : layer;
	}

	public function toString():String
		return stylesheet + " " + selector + " (rule " + rule + ", " + layer + ")";
}
